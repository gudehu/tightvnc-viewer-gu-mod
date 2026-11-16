// HU patch: shared helpers for the "name already exists" prompt.
// Header-only so both ft-client-lib and tvnviewer can use the exact same
// naming rule without introducing a new link dependency.

#ifndef _HU_RENAME_SUPPORT_H_
#define _HU_RENAME_SUPPORT_H_

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <string>

#include "util/StringStorage.h"

typedef std::basic_string<TCHAR> HuTString;

// "report.txt"      -> "report_2026-11-14.txt"
// "report.txt", 2   -> "report_2026-11-14_2.txt"
// "folder"          -> "folder_2026-11-14"
inline void HuMakeDatedName(const TCHAR *original, int seq, StringStorage *out)
{
  HuTString name(original ? original : _T(""));
  if (name.empty()) name = _T("file");

  HuTString base = name;
  HuTString ext;
  size_t dot = name.find_last_of(_T('.'));
  if (dot != HuTString::npos && dot > 0 && dot + 1 < name.size()) {
    base = name.substr(0, dot);
    ext = name.substr(dot);
  }

  SYSTEMTIME st;
  GetLocalTime(&st);
  TCHAR date[32];
  _sntprintf_s(date, 32, _TRUNCATE, _T("%04d-%02d-%02d"),
               st.wYear, st.wMonth, st.wDay);

  TCHAR buf[MAX_PATH * 2];
  if (seq <= 1)
    _sntprintf_s(buf, MAX_PATH * 2, _TRUNCATE, _T("%s_%s%s"),
                 base.c_str(), date, ext.c_str());
  else
    _sntprintf_s(buf, MAX_PATH * 2, _TRUNCATE, _T("%s_%s_%d%s"),
                 base.c_str(), date, seq, ext.c_str());
  out->setString(buf);
}

// Replace every path SEGMENT equal to "from" with "to". Both '/' and '\\'
// are treated as separators, so this works for local paths (C:\...) and for
// TightVNC FT wire paths (/C:/...).
inline void HuReplaceTargetName(StringStorage *path,
                                const TCHAR *from, const TCHAR *to)
{
  if (!path || !from || !*from || !to || !*to) return;
  const TCHAR *raw = path->getString();
  if (!raw) return;
  HuTString s(raw);
  HuTString f(from), t(to);
  if (f.empty() || f == t) return;

  size_t pos = 0;
  while ((pos = s.find(f, pos)) != HuTString::npos) {
    size_t end = pos + f.size();
    bool leftOk = (pos == 0) || (s[pos - 1] == _T('/')) || (s[pos - 1] == _T('\\'));
    bool rightOk = (end == s.size()) ||
                   (s[end] == _T('/')) || (s[end] == _T('\\'));
    if (leftOk && rightOk) {
      s.replace(pos, f.size(), t);
      pos += t.size();
    } else {
      pos += f.size();
    }
  }
  path->setString(s.c_str());
}

#endif // _HU_RENAME_SUPPORT_H_
