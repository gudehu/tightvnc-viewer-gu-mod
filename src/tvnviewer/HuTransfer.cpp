// Copyright (C) 2026 HU patch for TightVNC (chenall fork).
// Modeless progress window, date-named txt log, drag&drop upload and
// hotkey download built on top of the official TightVNC file-transfer core.

#include "HuTransfer.h"

#include <shellapi.h>
#include <commctrl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "comctl32.lib")

#include "ft-client-lib/CopyFileEventListener.h"
#include "ft-client-lib/HuRenameSupport.h"
#include "util/Exception.h"

// FileTransferCore state values (kept local: upstream declares them in a
// non-public section of the header).
static const int HU_ST_NOTHING   = 0;
static const int HU_ST_FILELIST  = 1;
static const int HU_ST_MKDIR     = 3;
static const int HU_ST_UPLOAD    = 6;
static const int HU_ST_DOWNLOAD  = 7;
static const int HU_ST_FATAL     = -1;

static const wchar_t *HU_SEND_DIR  = L"/C:/HU-vnc-send";
static const wchar_t *HU_SEND_PARENT = L"/C:";
static const wchar_t *HU_SEND_NAME   = L"HU-vnc-send";
static const wchar_t *HU_LOCAL_DIR   = L"C:\\HU-vnc-get";

// Child control ids of the progress window.
#define HU_ID_TITLE     1
#define HU_ID_BAR       2
#define HU_ID_PERCENT   3
#define HU_ID_STATUS    4
#define HU_ID_LOG       5
#define HU_ID_CANCEL    6

static CRITICAL_SECTION g_logLock;
static bool g_logLockInit = false;

// ---------------------------------------------------------------------------
// Date-named txt log in C:\HU-vnc-get
// ---------------------------------------------------------------------------
static std::wstring huTimeStamp()
{
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t b[64];
  swprintf_s(b, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return std::wstring(b);
}

static void huAppendLogFile(const wchar_t *direction, const wchar_t *text)
{
  if (!g_logLockInit) return;
  EnterCriticalSection(&g_logLock);
  CreateDirectoryW(HU_LOCAL_DIR, NULL);

  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t path[MAX_PATH];
  swprintf_s(path, MAX_PATH, L"%s\\%04d-%02d-%02d.txt",
             HU_LOCAL_DIR, st.wYear, st.wMonth, st.wDay);

  std::wstring line = L"[" + huTimeStamp() + L"] [" +
                      (direction ? direction : L"-") + L"] " +
                      (text ? text : L"") + L"\r\n";
  int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, NULL, 0, NULL, NULL);
  if (n > 1) {
    std::string u8((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, &u8[0], n, NULL, NULL);
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
      DWORD written = 0;
      if (GetFileSize(h, NULL) == 0) {
        static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        WriteFile(h, bom, 3, &written, NULL);
      }
      WriteFile(h, u8.data(), (DWORD)(n - 1), &written, NULL);
      CloseHandle(h);
    }
  }
  LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------------------
// Name-collision prompt. It is a MODELESS window: it never disables the
// viewer main window, so remote control stays usable while the user decides.
// The FT receive thread waits on an event while pumping its own messages.
// ---------------------------------------------------------------------------
#define HU_COLLISION_CLASS  L"TvnHuCollisionClass"
#define HUC_INFO       1
#define HUC_PATH       2
#define HUC_SUGGEST    3
#define HUC_OVERWRITE  101
#define HUC_SKIP       102
#define HUC_RENAME     103
#define HUC_CANCEL     104
#define HUC_APPLYALL   105

struct HuCollisionCtx
{
  HANDLE event;
  int action;
  bool applyAll;
  bool isDirectory;
  std::wstring name;
  std::wstring path;
  std::wstring suggested;
};

static LRESULT CALLBACK HuCollisionWndProc(HWND hwnd, UINT msg,
                                           WPARAM wp, LPARAM lp)
{
  HuCollisionCtx *ctx = reinterpret_cast<HuCollisionCtx *>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
  case WM_CREATE:
    {
      CREATESTRUCT *cs = reinterpret_cast<CREATESTRUCT *>(lp);
      ctx = reinterpret_cast<HuCollisionCtx *>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
      HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
      HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      HWND c;
      std::wstring head = ctx->isDirectory ?
          L"发现同名文件夹：" : L"发现同名文件：";
      head += ctx->name;
      c = CreateWindowW(L"Static", head.c_str(), WS_CHILD | WS_VISIBLE,
                        16, 14, 520, 24, hwnd, (HMENU)HUC_INFO, hi, NULL);
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
      std::wstring where = L"位置：" + ctx->path;
      c = CreateWindowW(L"Static", where.c_str(),
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        16, 42, 520, 42, hwnd, (HMENU)HUC_PATH, hi, NULL);
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
      std::wstring sug = L"自动重命名将使用：" + ctx->suggested;
      c = CreateWindowW(L"Static", sug.c_str(), WS_CHILD | WS_VISIBLE,
                        16, 90, 520, 22, hwnd, (HMENU)HUC_SUGGEST, hi, NULL);
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
      c = CreateWindowW(L"Button", L"后续同名项都按此处理（本次传输）",
                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                        16, 118, 520, 22, hwnd, (HMENU)HUC_APPLYALL, hi, NULL);
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
      const wchar_t *labels[4] = {
          L"覆盖", L"跳过此项", L"自动重命名（加日期）", L"取消整个传输"
      };
      int ids[4] = { HUC_OVERWRITE, HUC_SKIP, HUC_RENAME, HUC_CANCEL };
      int xs[4] = { 16, 151, 286, 446 };
      int ws[4] = { 125, 125, 150, 100 };
      for (int i = 0; i < 4; ++i) {
        c = CreateWindowW(L"Button", labels[i], WS_CHILD | WS_VISIBLE,
                          xs[i], 152, ws[i], 32, hwnd,
                          (HMENU)(INT_PTR)ids[i], hi, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
      }
      return 0;
    }
  case WM_COMMAND:
    {
      int id = LOWORD(wp);
      int act = -1;
      if (id == HUC_OVERWRITE) act = CopyFileEventListener::TFE_OVERWRITE;
      else if (id == HUC_SKIP) act = CopyFileEventListener::TFE_SKIP;
      else if (id == HUC_RENAME) act = CopyFileEventListener::TFE_RENAME;
      else if (id == HUC_CANCEL) act = CopyFileEventListener::TFE_CANCEL;
      if (act >= 0 && ctx) {
        ctx->action = act;
        HWND cb = GetDlgItem(hwnd, HUC_APPLYALL);
        ctx->applyAll = (cb != 0 &&
            SendMessageW(cb, BM_GETCHECK, 0, 0) == BST_CHECKED);
        SetEvent(ctx->event);
        return 0;
      }
      break;
    }
  case WM_CLOSE:
    if (ctx) {
      ctx->action = CopyFileEventListener::TFE_CANCEL;
      SetEvent(ctx->event);
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static void huRegisterCollisionClass()
{
  static bool done = false;
  if (done) return;
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = HuCollisionWndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = HU_COLLISION_CLASS;
  RegisterClassExW(&wc);
  done = true;
}

static int huWaitCollision(const wchar_t *name, const wchar_t *path,
                           bool isDir, const wchar_t *suggested,
                           bool *applyAll)
{
  huRegisterCollisionClass();
  HuCollisionCtx ctx;   // contains std::wstring: never memset this
  ctx.event = CreateEventW(NULL, TRUE, FALSE, NULL);
  ctx.action = CopyFileEventListener::TFE_OVERWRITE;
  ctx.applyAll = false;
  ctx.isDirectory = isDir;
  ctx.name = name ? name : L"";
  ctx.path = path ? path : L"";
  ctx.suggested = suggested ? suggested : L"";

  int w = 560, h = 230;
  int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  HWND hwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW, HU_COLLISION_CLASS,
      L"文件重名 - 请选择处理方式",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
      x, y, w, h, NULL, NULL, GetModuleHandleW(NULL), &ctx);
  if (!hwnd) {
    CloseHandle(ctx.event);
    return CopyFileEventListener::TFE_OVERWRITE;
  }
  ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);

  // IMPORTANT: MsgWaitForMultipleObjects returns immediately whenever input
  // is queued, so a "loop count * 1000 ms" timeout fires within milliseconds
  // and silently auto-answers the prompt. Use a real clock instead.
  const DWORD started = GetTickCount();
  while (true) {
    DWORD r = MsgWaitForMultipleObjects(1, &ctx.event, FALSE, 500,
                                        QS_ALLINPUT);
    if (r == WAIT_OBJECT_0) break;
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message != WM_QUIT) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
    if (GetTickCount() - started >= 120000) {  // safe default: auto rename
      ctx.action = CopyFileEventListener::TFE_RENAME;
      break;
    }
  }
  *applyAll = ctx.applyAll;
  int action = ctx.action;
  DestroyWindow(hwnd);
  CloseHandle(ctx.event);
  return action;
}

// ---------------------------------------------------------------------------
// Non-blocking FileTransferInterface implementation.
// All callbacks arrive on the FT receive thread; they only post messages.
// ---------------------------------------------------------------------------
class HuTransferInterface : public FileTransferInterface
{
public:
  HuTransferInterface(FileTransferCore *core, HuTransfer *owner)
    : FileTransferInterface(core), m_owner(owner) {}

  virtual int onFtTargetFileExists(FileInfo *source, FileInfo *,
                                   const TCHAR *pathToTargetFile)
  {
    const wchar_t *name = source ? source->getFileName() : L"";
    bool isDir = source ? source->isDirectory() : false;
    std::wstring newName;
    int action = m_owner->collisionDecision(name, pathToTargetFile,
                                            isDir, &newName);
    // TFE_RENAME: the operation reads the new name from sourceFileInfo and
    // restores the original name itself (so the source path stays valid).
    if (action == CopyFileEventListener::TFE_RENAME && !newName.empty() && source)
      source->setFileName(newName.c_str());
    return action;
  }

  virtual void setProgress(double progress) { m_owner->postProgress(progress); }

  virtual void onFtOpError(const TCHAR *message)
  {
    m_owner->postLog(L"错误", message ? message : L"(unknown error)");
  }

  virtual void onFtOpInfo(const TCHAR *message)
  {
    m_owner->postLog(L"信息", message ? message : L"");
  }

  virtual void onFtOpStarted() {}
  virtual void onFtOpFinished(int state, int result)
  {
    m_owner->postFinished(state, result);
  }
  virtual void setNothingState() {}
  virtual void onRefreshLocalFileList() {}
  virtual void onRefreshRemoteFileList() {}

  virtual void raise(Exception &ex)
  {
    m_owner->postLog(L"错误", ex.getMessage());
    m_owner->postFinished(HU_ST_FATAL, 0);
    // Deliberately NOT rethrowing: an exception crossing the FT receive
    // thread would kill the connection.
  }

private:
  HuTransfer *m_owner;
};

// ---------------------------------------------------------------------------
// Modeless progress window
// ---------------------------------------------------------------------------
static LRESULT CALLBACK HuProgressWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  HuTransfer *self = reinterpret_cast<HuTransfer *>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
  case WM_CREATE:
    {
      CREATESTRUCT *cs = reinterpret_cast<CREATESTRUCT *>(lp);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
      HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
      HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      HWND c;
      c = CreateWindowW(L"Static", L"准备传输…", WS_CHILD | WS_VISIBLE,
                        14, 12, 430, 22, hwnd, (HMENU)HU_ID_TITLE, hi, NULL);
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
      c = CreateWindowW(L"msctls_progress32", NULL, WS_CHILD | WS_VISIBLE,
                        14, 40, 430, 22, hwnd, (HMENU)HU_ID_BAR, hi, NULL);
      SendMessageW(c, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
      c = CreateWindowW(L"Static", L"0%", WS_CHILD | WS_VISIBLE,
                        14, 66, 430, 20, hwnd, (HMENU)HU_ID_PERCENT, hi, NULL);
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
      c = CreateWindowW(L"Static", L"", WS_CHILD | WS_VISIBLE,
                        14, 90, 430, 20, hwnd, (HMENU)HU_ID_STATUS, hi, NULL);
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
      c = CreateWindowW(L"ListBox", NULL,
                        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
                        LBS_DISABLENOSCROLL | LBS_NOSEL,
                        14, 114, 430, 176, hwnd, (HMENU)HU_ID_LOG, hi, NULL);
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
      c = CreateWindowW(L"Button", L"取消传输", WS_CHILD | WS_VISIBLE,
                        344, 298, 100, 28, hwnd, (HMENU)HU_ID_CANCEL, hi, NULL);
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
      return 0;
    }
  case HU_WM_PROGRESS:
    {
      HWND bar = GetDlgItem(hwnd, HU_ID_BAR);
      HWND pct = GetDlgItem(hwnd, HU_ID_PERCENT);
      int v = (int)wp;
      if (v < 0) v = 0;
      if (v > 1000) v = 1000;
      if (bar) SendMessageW(bar, PBM_SETPOS, v, 0);
      if (pct) {
        wchar_t b[32];
        swprintf_s(b, 32, L"%d%%", v / 10);
        SetWindowTextW(pct, b);
      }
      return 0;
    }
  case HU_WM_LOG:
    {
      wchar_t *line = reinterpret_cast<wchar_t *>(lp);
      if (line) {
        HWND box = GetDlgItem(hwnd, HU_ID_LOG);
        if (box) {
          // keep at most 600 visible lines
          LRESULT cnt = SendMessageW(box, LB_GETCOUNT, 0, 0);
          while (cnt >= 600) {
            SendMessageW(box, LB_DELETESTRING, 0, 0);
            cnt = SendMessageW(box, LB_GETCOUNT, 0, 0);
          }
          LRESULT idx = SendMessageW(box, LB_ADDSTRING, 0, (LPARAM)line);
          if (idx != LB_ERR)
            SendMessageW(box, LB_SETTOPINDEX, idx, 0);
        }
        delete[] line;
      }
      return 0;
    }
  case HU_WM_FINISHED:
    if (self) {
      int st = (int)(short)LOWORD(wp);
      int batch = (int)(short)HIWORD(wp);
      self->onOperationFinished(st, (int)lp, batch);
    }
    return 0;
  case HU_WM_NEXT:
    if (self) self->nextStep((int)wp);
    return 0;
  case HU_WM_HINT:
    if (self) self->log(L"提示", L"在远程电脑选中文件后，按 Shift+右键 →「复制文件地址」(Win11 可 Ctrl+Shift+C)，再按 Ctrl+Alt+G 取回；也可把路径直接贴到本机剪贴板。");
    return 0;
  case WM_COMMAND:
    if (LOWORD(wp) == HU_ID_CANCEL && self) {
      self->cancel();
      return 0;
    }
    break;
  case WM_TIMER:
    if (wp == 1) {
      KillTimer(hwnd, 1);
      DestroyWindow(hwnd);
      return 0;
    }
    break;
  case WM_CLOSE:
    if (self && self->busy()) {
      self->cancel();
      return 0;
    }
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    KillTimer(hwnd, 1);
    if (self) self->setProgressWindowClosed();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::wstring huTrim(const std::wstring &s)
{
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
  while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' ||
                   s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
  return s.substr(a, b - a);
}

static void huSplitLines(const std::wstring &text, std::vector<std::wstring> &out)
{
  std::wstring cur;
  for (size_t i = 0; i < text.size(); ++i) {
    wchar_t ch = text[i];
    if (ch == L'\r' || ch == L'\n') {
      std::wstring t = huTrim(cur);
      if (!t.empty()) out.push_back(t);
      cur.clear();
    } else {
      cur += ch;
    }
  }
  std::wstring t = huTrim(cur);
  if (!t.empty()) out.push_back(t);
}

static std::wstring huStripQuotes(std::wstring s)
{
  if (s.size() >= 2 && s[0] == L'"' && s[s.size() - 1] == L'"')
    return s.substr(1, s.size() - 2);
  return s;
}

static std::wstring huParentOf(const std::wstring &path)
{
  size_t p = path.find_last_of(L"\\/");
  if (p == std::wstring::npos) return L"";
  if (p == 2 && path.size() >= 3 && path[1] == L':') return path.substr(0, 3);
  return path.substr(0, p);
}

static std::wstring huBaseName(const std::wstring &path)
{
  size_t p = path.find_last_of(L"\\/");
  if (p == std::wstring::npos) return path;
  return path.substr(p + 1);
}

// Convert a Windows path (C:\dir\file) to the TightVNC FT wire path
// (/C:/dir/file) and split it into parent + file name.
static bool huToRemoteFtPath(const std::wstring &raw,
                             std::wstring &parent, std::wstring &name)
{
  std::wstring s = huStripQuotes(huTrim(raw));
  if (s.empty()) return false;
  for (size_t i = 0; i < s.size(); ++i)
    if (s[i] == L'\\') s[i] = L'/';

  bool absolute = false;
  if (s.size() >= 3 && s[1] == L':' &&
      ((s[0] >= L'A' && s[0] <= L'Z') || (s[0] >= L'a' && s[0] <= L'z'))) {
    s = L"/" + s;                 // C:/x  -> /C:/x
    absolute = true;
  } else if (s.size() >= 2 && s[0] == L'/' && s[1] == L'/') {
    absolute = true;              // UNC -> //server/share/...
  } else if (!s.empty() && s[0] == L'/') {
    absolute = true;              // already an FT path
  }
  if (!absolute) return false;
  while (s.size() > 1 && s[s.size() - 1] == L'/') s.erase(s.size() - 1);

  size_t p = s.find_last_of(L'/');
  if (p == std::wstring::npos || p + 1 >= s.size()) return false;
  parent = (p == 0) ? L"/" : s.substr(0, p);
  name = s.substr(p + 1);
  return !name.empty();
}

static bool huFileInfoOf(const std::wstring &full, const wchar_t *name,
                         FileInfo &out)
{
  WIN32_FILE_ATTRIBUTE_DATA fad;
  if (!GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &fad))
    return false;
  bool isDir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  UINT64 size = isDir ? 0 :
      (((UINT64)fad.nFileSizeHigh << 32) | (UINT64)fad.nFileSizeLow);
  out = FileInfo(size, 0, isDir ? FileInfo::DIRECTORY : 0, name);
  return true;
}

static void huRegisterClass()
{
  static bool done = false;
  if (done) return;
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = HuProgressWndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = HU_PROGRESS_CLASS;
  RegisterClassExW(&wc);
  done = true;
}

// ---------------------------------------------------------------------------
// HuTransfer
// ---------------------------------------------------------------------------
HuTransfer::HuTransfer()
  : m_ft(0), m_core(0), m_interface(0), m_mainWnd(0), m_progressWnd(0),
    m_busy(false), m_cancelled(false), m_groupCount(0), m_groupIndex(0),
    m_okCount(0), m_failCount(0), m_errorCount(0), m_phase(PH_IDLE),
    m_listIndex(0), m_collisionPolicy(-1), m_collisionCancelled(false),
    m_batchId(0)
{
  m_direction[0] = 0;
  if (!g_logLockInit) {
    InitializeCriticalSection(&g_logLock);
    g_logLockInit = true;
  }
}

HuTransfer::~HuTransfer() {}

HuTransfer *HuTransfer::instance()
{
  static HuTransfer s;
  return &s;
}

void HuTransfer::init(FileTransferCapability *ft, HWND mainWnd)
{
  m_ft = ft;
  m_core = ft ? ft->getCore() : 0;
  m_mainWnd = mainWnd;
  CreateDirectoryW(HU_LOCAL_DIR, NULL);
}

void HuTransfer::setMainWnd(HWND h) { m_mainWnd = h; }

void HuTransfer::shutdown()
{
  if (m_progressWnd) {
    DestroyWindow(m_progressWnd);
    m_progressWnd = 0;
  }
  m_busy = false;
  m_phase = PH_IDLE;
}

bool HuTransfer::ensureWindow()
{
  huRegisterClass();
  if (m_progressWnd && IsWindow(m_progressWnd)) {
    KillTimer(m_progressWnd, 1);
    ShowWindow(m_progressWnd, SW_SHOW);
    SetWindowPos(m_progressWnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    return true;
  }
  int w = 470, h = 350;
  RECT rc;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
  int x = rc.right - w - 20;
  int y = rc.bottom - h - 20;
  m_progressWnd = CreateWindowExW(
      WS_EX_TOOLWINDOW, HU_PROGRESS_CLASS, L"文件传输 - TightVNC",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      x, y, w, h, NULL, NULL, GetModuleHandleW(NULL), this);
  if (!m_progressWnd) return false;
  ShowWindow(m_progressWnd, SW_SHOW);
  return true;
}

void HuTransfer::ensureInterface()
{
  if (!m_core) return;
  if (!m_interface)
    m_interface = new HuTransferInterface(m_core, this);
  if (m_ft) m_ft->setInterface(m_interface);
}

void HuTransfer::log(const wchar_t *direction, const wchar_t *text)
{
  if (!text) return;
  huAppendLogFile(direction, text);
  std::wstring line = L"[" + huTimeStamp().substr(11) + L"] " + text;
  if (m_progressWnd && IsWindow(m_progressWnd)) {
    size_t n = line.size() + 1;
    wchar_t *buf = new wchar_t[n];
    wcscpy_s(buf, n, line.c_str());
    PostMessageW(m_progressWnd, HU_WM_LOG, 0, (LPARAM)buf);
  }
}

void HuTransfer::logFmt(const wchar_t *direction, const wchar_t *fmt, ...)
{
  wchar_t b[1024];
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf_s(b, 1024, _TRUNCATE, fmt, ap);
  va_end(ap);
  log(direction, b);
}

void HuTransfer::postLog(const wchar_t *direction, const wchar_t *text)
{
  if (!text) return;
  // Downgrade the harmless "folder already exists" NACK into an info line.
  if (m_phase == PH_MKDIR &&
      wcsstr(text, L"failed to create remote folder") != 0) {
    huAppendLogFile(L"信息", L"目标目录已存在，继续发送");
    return;
  }
  if (direction && wcscmp(direction, L"错误") == 0) ++m_errorCount;
  huAppendLogFile(direction, text);
  std::wstring line = L"[" + huTimeStamp().substr(11) + L"] " + text;
  if (m_progressWnd && IsWindow(m_progressWnd)) {
    size_t n = line.size() + 1;
    wchar_t *buf = new wchar_t[n];
    wcscpy_s(buf, n, line.c_str());
    if (!PostMessageW(m_progressWnd, HU_WM_LOG, 0, (LPARAM)buf))
      delete[] buf;
  }
}

void HuTransfer::postProgress(double progress)
{
  if (!m_progressWnd || !IsWindow(m_progressWnd)) return;
  int v = (int)(progress * 1000.0);
  if (v < 0) v = 0;
  if (v > 1000) v = 1000;
  PostMessageW(m_progressWnd, HU_WM_PROGRESS, (WPARAM)v, 0);
}

void HuTransfer::postFinished(int state, int result)
{
  if (!m_progressWnd || !IsWindow(m_progressWnd)) return;
  WPARAM packed = (WPARAM)(((DWORD)state & 0xFFFF) |
                          (((DWORD)m_batchId & 0xFFFF) << 16));
  PostMessageW(m_progressWnd, HU_WM_FINISHED, packed, (LPARAM)result);
}

static const wchar_t *huActionName(int action)
{
  switch (action) {
  case CopyFileEventListener::TFE_OVERWRITE: return L"覆盖";
  case CopyFileEventListener::TFE_SKIP:      return L"跳过此项";
  case CopyFileEventListener::TFE_RENAME:    return L"自动重命名";
  case CopyFileEventListener::TFE_CANCEL:    return L"取消整个传输";
  }
  return L"未知";
}

int HuTransfer::collisionDecision(const wchar_t *name,
                                  const wchar_t *targetPath,
                                  bool isDirectory,
                                  std::wstring *newName)
{
  std::wstring n = name ? name : L"";
  // Sequence grows for the whole process lifetime, so two collisions in
  // different batches never produce the same generated name.
  int seq = ++m_renameSeq[n];
  StringStorage dated;
  HuMakeDatedName(n.c_str(), seq, &dated);
  std::wstring suggested = dated.getString();

  int action;
  const wchar_t *kind = isDirectory ? L"文件夹" : L"文件";
  if (m_collisionPolicy >= 0) {
    action = m_collisionPolicy;
    logFmt(m_direction, L"同名%s「%s」按已选策略处理：%s",
           kind, n.c_str(), huActionName(action));
  } else {
    bool applyAll = false;
    action = huWaitCollision(n.c_str(), targetPath, isDirectory,
                             suggested.c_str(), &applyAll);
    if (applyAll) m_collisionPolicy = action;
    logFmt(m_direction, L"同名%s「%s」，用户选择：%s%s",
           kind, n.c_str(), huActionName(action),
           applyAll ? L"（应用到后续同名项）" : L"");
  }
  if (action == CopyFileEventListener::TFE_RENAME) {
    if (newName) *newName = suggested;
    logFmt(m_direction, L"自动重命名为：%s", suggested.c_str());
  } else if (action == CopyFileEventListener::TFE_CANCEL) {
    m_collisionCancelled = true;
    log(m_direction, L"用户选择取消整个传输，本次不再传后续项目");
  }
  return action;
}

void HuTransfer::resetQueue()
{
  m_uploadGroups.clear();
  m_uploadRoots.clear();
  m_listParents.clear();
  m_getItems.clear();
  m_getGroups.clear();
  m_listIndex = 0;
  m_groupIndex = 0;
  m_groupCount = 0;
}

void HuTransfer::beginBatch(const wchar_t *tag)
{
  resetQueue();
  CreateDirectoryW(HU_LOCAL_DIR, NULL);
  ensureWindow();
  ensureInterface();
  wcscpy_s(m_direction, 16, tag);
  m_busy = true;
  m_cancelled = false;
  m_okCount = 0;
  m_failCount = 0;
  m_errorCount = 0;
  m_collisionPolicy = -1;   // ask again for every new batch
  m_collisionCancelled = false;
  ++m_batchId;   // stale messages of a cancelled batch are ignored
  m_phase = PH_IDLE;
  if (m_progressWnd) {
    SetWindowTextW(m_progressWnd,
        wcscmp(tag, L"发送") == 0 ? L"发送到 C:\\HU-vnc-send" :
                                   L"取回到 C:\\HU-vnc-get");
    HWND t = GetDlgItem(m_progressWnd, HU_ID_TITLE);
    if (t) SetWindowTextW(t,
        wcscmp(tag, L"发送") == 0 ? L"正在发送文件到远程电脑…" :
                                   L"正在取回远程文件到本机…");
    HWND st = GetDlgItem(m_progressWnd, HU_ID_STATUS);
    if (st) SetWindowTextW(st, L"准备中…");
    HWND b = GetDlgItem(m_progressWnd, HU_ID_CANCEL);
    if (b) SetWindowTextW(b, L"取消传输");
    HWND bar = GetDlgItem(m_progressWnd, HU_ID_BAR);
    if (bar) SendMessageW(bar, PBM_SETPOS, 0, 0);
    KillTimer(m_progressWnd, 1);
  }
  logFmt(m_direction, L"===== 开始%s：时间 %s =====", tag, huTimeStamp().c_str());
}

void HuTransfer::finishBatch(bool ok, const wchar_t *summary)
{
  if (!m_busy) return;
  if (m_collisionCancelled) {
    ok = false;
    summary = L"已取消（同名冲突时选择取消整个传输）";
  }
  m_busy = false;
  m_phase = PH_IDLE;
  if (m_progressWnd && IsWindow(m_progressWnd)) {
    HWND st = GetDlgItem(m_progressWnd, HU_ID_STATUS);
    if (st) SetWindowTextW(st, summary);
    HWND b = GetDlgItem(m_progressWnd, HU_ID_CANCEL);
    if (b) SetWindowTextW(b, L"关闭");
    HWND bar = GetDlgItem(m_progressWnd, HU_ID_BAR);
    if (bar && ok) SendMessageW(bar, PBM_SETPOS, 1000, 0);
    SetTimer(m_progressWnd, 1, 10000, NULL);   // auto-close, still modeless
  }
  logFmt(m_direction, L"===== 结束%s：%s =====", m_direction, summary);
  (void)ok;
}

// ---- SEND -----------------------------------------------------------------
void HuTransfer::startUpload(const std::vector<std::wstring> &localPaths)
{
  if (!m_core || !m_ft) {
    log(L"错误", L"尚未连接或服务端不支持文件传输，无法发送");
    return;
  }
  if (m_busy) {
    log(L"提示", L"已有传输在进行，请等待完成后再拖放");
    return;
  }
  if (localPaths.empty()) return;

  beginBatch(L"发送");

  // Group by local parent folder: uploadOperation() takes one source root.
  for (size_t i = 0; i < localPaths.size(); ++i) {
    std::wstring full = localPaths[i];
    std::wstring parent = huParentOf(full);
    std::wstring name = huBaseName(full);
    if (parent.empty() || name.empty()) continue;
    size_t g = (size_t)-1;
    for (size_t j = 0; j < m_uploadRoots.size(); ++j)
      if (_wcsicmp(m_uploadRoots[j].c_str(), parent.c_str()) == 0) { g = j; break; }
    if (g == (size_t)-1) {
      m_uploadRoots.push_back(parent);
      m_uploadGroups.push_back(std::vector<std::wstring>());
      g = m_uploadRoots.size() - 1;
    }
    m_uploadGroups[g].push_back(name);
    logFmt(m_direction, L"待发送：%s", full.c_str());
  }
  m_groupCount = (int)m_uploadGroups.size();
  if (m_groupCount == 0) {
    finishBatch(false, L"没有可发送的文件");
    return;
  }
  logFmt(m_direction, L"共 %d 个项目，目标目录 %s",
         (int)localPaths.size(), L"C:\\HU-vnc-send");
  startMkdirThenUpload();
}

void HuTransfer::startMkdirThenUpload()
{
  if (!m_core) return;
  m_phase = PH_MKDIR;
  m_groupIndex = 0;
  // Harmless when the folder already exists: the server NACKs and we
  // downgrade that message to an info line.
  FileInfo fi(0, 0, FileInfo::DIRECTORY, HU_SEND_NAME);
  try {
    m_core->remoteFolderCreateOperation(fi, HU_SEND_PARENT);
  } catch (...) {
    // Fall back to uploading directly (folder may already exist).
    log(m_direction, L"创建目标目录请求异常，直接尝试发送");
    PostMessageW(m_progressWnd, HU_WM_NEXT, (WPARAM)m_batchId, 0);
  }
}

void HuTransfer::startUploadGroup()
{
  if (!m_core || m_groupIndex >= m_groupCount) {
    wchar_t s[256];
    swprintf_s(s, 256, L"发送完成：%d 组，错误 %d 条，无效项 %d 个",
               m_okCount, m_errorCount, m_failCount);
    finishBatch(m_errorCount == 0 && m_failCount == 0, s);
    return;
  }
  int g = m_groupIndex++;
  std::vector<FileInfo> infos;
  for (size_t i = 0; i < m_uploadGroups[g].size(); ++i) {
    std::wstring name = m_uploadGroups[g][i];
    std::wstring full = m_uploadRoots[g];
    if (!full.empty() && full[full.size() - 1] != L'\\') full += L"\\";
    full += name;
    FileInfo fi;
    if (!huFileInfoOf(full, name.c_str(), fi)) {
      logFmt(m_direction, L"跳过（读取失败）：%s", full.c_str());
      ++m_failCount;
      continue;
    }
    infos.push_back(fi);
    logFmt(m_direction, L"发送 [%d/%d] %s", g + 1, m_groupCount, name.c_str());
  }
  if (infos.empty()) {
    PostMessageW(m_progressWnd, HU_WM_NEXT, (WPARAM)m_batchId, 0);
    return;
  }
  m_phase = PH_UPLOAD;
  try {
    m_core->uploadOperation(&infos[0], infos.size(),
                            m_uploadRoots[g].c_str(), HU_SEND_DIR);
  } catch (...) {
    ++m_failCount;
    log(m_direction, L"启动上传失败");
    PostMessageW(m_progressWnd, HU_WM_NEXT, (WPARAM)m_batchId, 0);
  }
}

void HuTransfer::handleDrop(HDROP hDrop)
{
  if (!hDrop) return;
  UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
  std::vector<std::wstring> paths;
  for (UINT i = 0; i < n; ++i) {
    UINT len = DragQueryFileW(hDrop, i, NULL, 0);
    if (len == 0) continue;
    std::wstring p(len + 1, L'\0');
    DragQueryFileW(hDrop, i, &p[0], len + 1);
    p.resize(wcslen(p.c_str()));
    if (!p.empty()) paths.push_back(p);
  }
  DragFinish(hDrop);
  if (!paths.empty()) startUpload(paths);
}

// ---- GET ------------------------------------------------------------------
void HuTransfer::startGet(const std::vector<std::wstring> &remotePaths)
{
  if (!m_core || !m_ft) {
    log(L"错误", L"尚未连接或服务端不支持文件传输，无法取回");
    return;
  }
  if (m_busy) {
    log(L"提示", L"已有传输在进行，请等待完成后再取回");
    return;
  }
  beginBatch(L"接收");

  CreateDirectoryW(HU_LOCAL_DIR, NULL);
  for (size_t i = 0; i < remotePaths.size(); ++i) {
    std::wstring parent, name;
    if (!huToRemoteFtPath(remotePaths[i], parent, name)) {
      logFmt(m_direction, L"路径无法识别：%s", remotePaths[i].c_str());
      ++m_failCount;
      continue;
    }
    GetItem it;
    it.remoteParent = parent;
    it.fileName = name;
    it.localTarget = HU_LOCAL_DIR;
    m_getItems.push_back(it);
    bool seen = false;
    for (size_t j = 0; j < m_listParents.size(); ++j)
      if (m_listParents[j] == parent) { seen = true; break; }
    if (!seen) m_listParents.push_back(parent);
    logFmt(m_direction, L"待取回：%s (目录 %s)", name.c_str(), parent.c_str());
  }
  if (m_getItems.empty()) {
    finishBatch(false, L"没有有效的远程路径");
    return;
  }
  m_listIndex = 0;
  startNextFileList();
}

void HuTransfer::startNextFileList()
{
  if (!m_core) return;
  if (m_listIndex >= m_listParents.size()) {
    m_groupIndex = 0;
    m_groupCount = (int)m_getGroups.size();
    startDownloadGroup();
    return;
  }
  std::wstring parent = m_listParents[m_listIndex++];
  m_phase = PH_LIST;
  logFmt(m_direction, L"读取远程目录清单：%s", parent.c_str());
  try {
    m_core->remoteFileListOperation(parent.c_str());
  } catch (...) {
    logFmt(m_direction, L"读取远程目录失败：%s", parent.c_str());
    m_failCount += 1;
    PostMessageW(m_progressWnd, HU_WM_NEXT, (WPARAM)m_batchId, 0);
  }
}

void HuTransfer::collectFileList(int result)
{
  std::wstring parent = m_listParents[m_listIndex - 1];
  std::vector<FileInfo> matched;
  if (result != 0 && m_core) {
    std::vector<FileInfo> *list = m_core->getListRemoteFolder();
    if (list) {
      for (size_t i = 0; i < m_getItems.size(); ++i) {
        if (m_getItems[i].remoteParent != parent) continue;
        bool found = false;
        for (size_t j = 0; j < list->size(); ++j) {
          const TCHAR *fn = (*list)[j].getFileName();
          if (fn && _wcsicmp(fn, m_getItems[i].fileName.c_str()) == 0) {
            matched.push_back((*list)[j]);
            logFmt(m_direction, L"匹配到：%s", fn);
            found = true;
            break;
          }
        }
        if (!found) {
          logFmt(m_direction, L"远程不存在：%s", m_getItems[i].fileName.c_str());
          ++m_failCount;
        }
      }
    }
  } else {
    logFmt(m_direction, L"远程目录不可读：%s", parent.c_str());
  }
  if (!matched.empty()) {
    GetGroup g;
    g.remoteParent = parent;
    g.files = matched;
    m_getGroups.push_back(g);
  }
}

void HuTransfer::startDownloadGroup()
{
  if (!m_core || m_groupIndex >= m_groupCount) {
    wchar_t s[256];
    swprintf_s(s, 256, L"取回完成：%d 组，错误 %d 条，无效项 %d 个",
               m_okCount, m_errorCount, m_failCount);
    finishBatch(m_errorCount == 0 && m_failCount == 0, s);
    return;
  }
  GetGroup &g = m_getGroups[m_groupIndex++];
  logFmt(m_direction, L"开始取回 [%d/%d]，来源 %s → %s",
         m_groupIndex, m_groupCount, g.remoteParent.c_str(), HU_LOCAL_DIR);
  m_phase = PH_DOWNLOAD;
  try {
    m_core->downloadOperation(&g.files[0], g.files.size(),
                              HU_LOCAL_DIR, g.remoteParent.c_str());
  } catch (...) {
    ++m_failCount;
    log(m_direction, L"启动取回失败");
    PostMessageW(m_progressWnd, HU_WM_NEXT, (WPARAM)m_batchId, 0);
  }
}

// ---- state machine --------------------------------------------------------
void HuTransfer::onOperationFinished(int state, int result, int batch)
{
  if (!m_core) return;
  if (batch != m_batchId || !m_busy) return;   // stale completion
  if (state == HU_ST_FATAL) {
    ++m_failCount;
    finishBatch(false, L"传输异常中止");
    return;
  }
  // Mirror FileTransferMainDialog::onMessageReceived(): the core must remove
  // the finished operation from the FT listener list, then update its state.
  m_core->onOperationFinished();
  m_core->onUpdateState(state, result);

  if (m_phase == PH_MKDIR) {
    PostMessageW(m_progressWnd, HU_WM_NEXT, (WPARAM)m_batchId, 0);
    return;
  }
  if (m_phase == PH_LIST) {
    collectFileList(result);
    PostMessageW(m_progressWnd, HU_WM_NEXT, (WPARAM)m_batchId, 0);
    return;
  }
  // NOTE: FileTransferCore::ftOpFinished() always passes result=0 for
  // non-FILE_LIST operations, so the result flag cannot be used to detect
  // success. Real failures arrive through onFtOpError()/onFtOpInfo() and
  // are counted in m_errorCount instead.
  if (m_phase == PH_UPLOAD || m_phase == PH_DOWNLOAD) {
    ++m_okCount;                 // one group finished
    (void)result;
    PostMessageW(m_progressWnd, HU_WM_NEXT, (WPARAM)m_batchId, 0);
    return;
  }
}

void HuTransfer::nextStep(int batch)
{
  if (batch != m_batchId || !m_busy || m_cancelled) return;
  switch (m_phase) {
  case PH_MKDIR:
    startUploadGroup();
    break;
  case PH_UPLOAD:
    startUploadGroup();
    break;
  case PH_LIST:
    startNextFileList();
    break;
  case PH_DOWNLOAD:
    startDownloadGroup();
    break;
  default:
    break;
  }
}

void HuTransfer::cancel()
{
  if (!m_busy) {
    if (m_progressWnd && IsWindow(m_progressWnd)) DestroyWindow(m_progressWnd);
    return;
  }
  m_cancelled = true;
  log(m_direction, L"用户取消传输");
  if (m_core) {
    try { m_core->terminateCurrentOperation(); } catch (...) {}
  }
  finishBatch(false, L"已取消");
}

// ---- hotkey / scripting ---------------------------------------------------
void HuTransfer::showHint()
{
  ensureWindow();
  log(L"提示", L"未找到远程路径。请在远程电脑选中文件/文件夹后按 Shift+右键 →「复制文件地址」(Win11: Ctrl+Shift+C)，再按 Ctrl+Alt+G；也可把路径直接贴进本机剪贴板。");
  if (m_progressWnd && IsWindow(m_progressWnd)) {
    HWND t = GetDlgItem(m_progressWnd, HU_ID_TITLE);
    if (t) SetWindowTextW(t, L"如何取回远程文件");
    HWND st = GetDlgItem(m_progressWnd, HU_ID_STATUS);
    if (st) SetWindowTextW(st, L"等待远程路径（Ctrl+Alt+G 重试）");
    HWND b = GetDlgItem(m_progressWnd, HU_ID_CANCEL);
    if (b) SetWindowTextW(b, L"关闭");
    SetTimer(m_progressWnd, 1, 20000, NULL);
  }
}

void HuTransfer::onHotkey()
{
  // Both the low-level keyboard hook and the desktop window may report the
  // same key press; ignore duplicates within 600 ms.
  static DWORD s_lastTick = 0;
  DWORD now = GetTickCount();
  if (now - s_lastTick < 600) return;
  s_lastTick = now;
  if (m_busy) {
    log(L"提示", L"已有传输在进行");
    return;
  }
  std::wstring clip;
  if (IsClipboardFormatAvailable(CF_UNICODETEXT) && OpenClipboard(NULL)) {
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
      const wchar_t *s = (const wchar_t *)GlobalLock(h);
      if (s) { clip = s; GlobalUnlock(h); }
    }
    CloseClipboard();
  }
  if (clip.empty() && IsClipboardFormatAvailable(CF_TEXT) && OpenClipboard(NULL)) {
    HANDLE h = GetClipboardData(CF_TEXT);
    if (h) {
      const char *s = (const char *)GlobalLock(h);
      if (s) {
        int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
        if (n > 0) {
          std::vector<wchar_t> b((size_t)n);
          MultiByteToWideChar(CP_ACP, 0, s, -1, &b[0], n);
          clip = &b[0];
        }
        GlobalUnlock(h);
      }
    }
    CloseClipboard();
  }
  std::vector<std::wstring> lines;
  huSplitLines(clip, lines);
  std::vector<std::wstring> paths;
  for (size_t i = 0; i < lines.size(); ++i) {
    std::wstring p, n;
    if (huToRemoteFtPath(lines[i], p, n)) paths.push_back(lines[i]);
  }
  if (paths.empty()) showHint();
  else startGet(paths);
}

bool HuTransfer::handleCopyData(LPARAM lParam)
{
  if (!lParam) return false;
  COPYDATASTRUCT *cds = reinterpret_cast<COPYDATASTRUCT *>(lParam);
  if (cds->dwData != 0x48554E43 || !cds->lpData || cds->cbData < 2) return false;
  std::wstring data((const wchar_t *)cds->lpData,
                    cds->cbData / sizeof(wchar_t));
  size_t z = data.find(L'\0');
  if (z != std::wstring::npos) data.resize(z);
  std::vector<std::wstring> parts;
  size_t start = 0;
  for (size_t i = 0; i <= data.size(); ++i) {
    if (i == data.size() || data[i] == L'\t') {
      std::wstring t = huTrim(data.substr(start, i - start));
      if (!t.empty()) parts.push_back(t);
      start = i + 1;
    }
  }
  if (parts.empty()) return true;
  if (_wcsicmp(parts[0].c_str(), L"SEND") == 0) {
    std::vector<std::wstring> paths(parts.begin() + 1, parts.end());
    startUpload(paths);
    return true;
  }
  if (_wcsicmp(parts[0].c_str(), L"GET") == 0) {
    std::vector<std::wstring> paths(parts.begin() + 1, parts.end());
    startGet(paths);
    return true;
  }
  if (_wcsicmp(parts[0].c_str(), L"HINT") == 0) {
    showHint();
    return true;
  }
  return true;
}

bool HuTransfer::handleCustomMessage(UINT msg, WPARAM, LPARAM)
{
  if (msg == HU_WM_HINT) { showHint(); return true; }
  return false;
}
