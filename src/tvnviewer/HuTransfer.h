// Copyright (C) 2026 HU patch for TightVNC (chenall fork).
// Drag & drop upload to C:\HU-vnc-send and hotkey download to C:\HU-vnc-get.
// Modeless floating progress window + date-named txt log. No modal dialogs.

#ifndef _HU_TRANSFER_H_
#define _HU_TRANSFER_H_

// winsock2.h MUST come before windows.h: the latter pulls in the legacy
// winsock.h, which redefines sockaddr/fd_set and breaks TightVNC headers.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <map>

#include "ft-client-lib/FileTransferInterface.h"
#include "viewer-core/FileTransferCapability.h"

// Posted messages (all UI work happens on the viewer's main thread).
#define HU_WM_PROGRESS   (WM_APP + 0x11)   // wParam = 0..1000
#define HU_WM_LOG        (WM_APP + 0x12)   // lParam = new wchar_t[] (owned by receiver)
#define HU_WM_FINISHED   (WM_APP + 0x13)   // wParam = state, lParam = result
#define HU_WM_NEXT       (WM_APP + 0x14)   // drive the next queued step
#define HU_WM_HINT       (WM_APP + 0x15)   // show "how to get files" help
#define HU_WM_HOTKEY     (WM_APP + 0x16)   // Ctrl+Alt+G, posted from the keyboard hook

#define HU_PROGRESS_CLASS  L"TvnHuProgressClass"

// Public entry points used from ViewerWindow / DesktopWindow.
class HuTransfer
{
public:
  static HuTransfer *instance();

  void init(FileTransferCapability *ft, HWND mainWnd);
  void setMainWnd(HWND h);
  void setProgressWindowClosed() { m_progressWnd = 0; }
  HWND mainWnd() const { return m_mainWnd; }
  bool ready() const { return m_core != 0; }
  bool busy() const { return m_busy; }

  // Drag & drop of local files/folders onto the viewer window.
  void handleDrop(HDROP hDrop);
  void startUpload(const std::vector<std::wstring> &localPaths);

  // Ctrl+Alt+G: paths from the local clipboard (synced from remote "Copy as path").
  void onHotkey();
  void startGet(const std::vector<std::wstring> &remotePaths);

  // WM_COPYDATA scripting/test hook: "SEND\t<path>\t..." or "GET\t<path>\t...".
  bool handleCopyData(LPARAM lParam);
  bool handleCustomMessage(UINT msg, WPARAM wParam, LPARAM lParam);

  // Called by the progress window procedure.
  void onOperationFinished(int state, int result, int batch);
  void nextStep(int batch);
  void cancel();
  void shutdown();

  // Ask the user what to do about a name collision. Returns a
  // CopyFileEventListener::TFE_* value; TFE_RENAME also fills newName.
  int collisionDecision(const wchar_t *name, const wchar_t *targetPath,
                        bool isDirectory, std::wstring *newName);

  void log(const wchar_t *direction, const wchar_t *text);
  void logFmt(const wchar_t *direction, const wchar_t *fmt, ...);

  // Non-blocking replacement for FileTransferInterface callbacks.
  void postLog(const wchar_t *direction, const wchar_t *text);
  void postProgress(double progress);
  void postFinished(int state, int result);

private:
  HuTransfer();
  ~HuTransfer();

  HuTransfer(const HuTransfer &);
  HuTransfer &operator=(const HuTransfer &);

  bool ensureWindow();
  void ensureInterface();
  void beginBatch(const wchar_t *tag);
  void finishBatch(bool ok, const wchar_t *summary);
  void resetQueue();

  void startMkdirThenUpload();
  void startUploadGroup();
  void startNextFileList();
  void collectFileList(int result);
  void startDownloadGroup();
  void showHint();

  FileTransferCapability *m_ft;
  FileTransferCore *m_core;
  FileTransferInterface *m_interface;
  HWND m_mainWnd;
  HWND m_progressWnd;
  bool m_busy;
  bool m_cancelled;
  int  m_groupCount;
  int  m_groupIndex;
  int  m_okCount;
  int  m_failCount;
  int  m_errorCount;   // errors reported by the FT core (thread-shared)
  wchar_t m_direction[16];   // L"发送" / L"接收"
  int  m_collisionPolicy;    // -1 = ask every time, otherwise a TFE_* value
  bool m_collisionCancelled; // user chose "cancel whole transfer" on a clash
  int  m_batchId;            // isolates queued messages of cancelled batches
  std::map<std::wstring, int> m_renameSeq;  // per-name sequence, never reset

  enum Phase {
    PH_IDLE = 0,
    PH_MKDIR,      // creating C:\HU-vnc-send on the remote machine
    PH_UPLOAD,     // uploading one group (one local parent folder)
    PH_LIST,       // collecting remote FileInfo for one parent folder
    PH_DOWNLOAD,   // downloading one group
  };
  Phase m_phase;

  // Upload: one group per distinct local parent folder.
  std::vector<std::vector<std::wstring> > m_uploadGroups;   // file names
  std::vector<std::wstring> m_uploadRoots;                 // local parent folder

  // Download: one group per distinct remote parent folder.
  struct GetItem {
    std::wstring remoteParent;   // FT path, e.g. /C:/Windows
    std::wstring fileName;       // e.g. win.ini
    std::wstring localTarget;    // C:\HU-vnc-get
  };
  struct GetGroup {
    std::wstring remoteParent;
    std::vector<FileInfo> files;
  };
  std::vector<std::wstring> m_listParents;
  std::vector<GetItem> m_getItems;
  std::vector<GetGroup> m_getGroups;
  size_t m_listIndex;
};

#endif // _HU_TRANSFER_H_
