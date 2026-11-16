# 修改清单（相对上游 TightVNC 2.8.90 GPL）

GNU GPL v2 第 2 条要求：修改后的文件须带有显著的修改声明，并对整个作品声明以相同许可证发布。本项目所有改动均在源码中以注释 `HU patch` / `HU_PATCH` 标注，便于对照上游。以下为主要修改的归纳。

## 一、来源与基线

- 基线：TightVNC 2.8.90 GPL 版源码（GlavSoft LLC，GPL v2）。
- 中间分支：`chenall/tightvnc`（提供 SERVER 端 INI 配置、默认密码、部分汉化等修改）。本发布仅包含 Viewer 相关改动；Server 端源码随上游源码原样保留。
- 本项目在其中文增强版 Viewer 基础上继续修改，版本标记为 "tightVNC-gu mod"。

## 二、Viewer 功能修改（本次）

1. **在原生 16 个按钮之后追加 12 个按钮**（新增命令 ID 216~227）
   - 传文件与启动器类（8 个）：
     - `发送文件`（223）：多选本地文件/文件夹，经 RFB 文件传输通道上传到远端 `C:\HU-vnc-send`；另支持拖放上传。
     - `获取文件`（216）：读取剪贴板中的远端路径，经文件传输通道下载到本机 `C:\HU-vnc-get`；也可按 Ctrl+Alt+G。
     - `get目录`（217）：在本机资源管理器打开 `C:\HU-vnc-get`。
     - `send目录`（218）：通过远端 Win+R 输入路径，在对方电脑打开 `C:\HU-vnc-send`。
     - `本地cmd`（219）：本机 ShellExecute 打开 cmd.exe。
     - `远程cmd`（220）：远端 Win+R 输入 cmd 并回车（剪贴板粘贴方式）。
     - `本地txt`（221）：本机 ShellExecute 打开 notepad.exe。
     - `远程txt`（222）：远端 Win+R 输入 notepad 并回车。
   - 控制体验增强类（4 个）：
     - `仅查看`（224）：一键切换 view-only，配套系统级热键 Ctrl+Shift+O。
     - `Candy热键`（225）：向远端发送 Ctrl+F8。
     - `滚动到顶`（226）：滚动条回到垂直位置 0。
     - `远端桌面`（227）：向远端发送 Win+D 切换显示桌面。
   - 涉及文件：`ViewerWindow.cpp/.h`、`resource.h`、`resource_chs.h`、`tvnviewer.rc`、新增位图 `res/hu_extra.bmp`、图标 `res/candy.ico`、`res/noentry.ico`，以及 `ToolBar.cpp/.h`、`HuTransfer.cpp/.h`。

2. **中文输入修复**
   - 在远程画面窗口创建时解除本地输入法上下文关联（`ImmAssociateContext(hwnd, NULL)`），修复"远端打中文时本地候选框跟着跳"的问题。
   - 涉及文件：`DesktopWindow.cpp`（引入 `<imm.h>` 与 `imm32.lib`）。

3. **边缘自动滚动优化**
   - 增大触发边距、提高滚动速度；鼠标移出窗口边框后仍沿该方向滚动到内容尽头；向上滚动预留 10 像素保护带。
   - 涉及文件：`DesktopWindow.cpp/.h`。

4. **远程运行命令改为剪贴板粘贴**
   - `sendRemoteRunCommand()` 通过 RFB ClientCutText 下发命令文本，再发送 Ctrl+V + Enter，避免逐字母输入被吞。
   - 新增 `DesktopWindow::pushClipboardText()` 绕过本地剪贴板去重逻辑。
   - 涉及文件：`ViewerWindow.cpp`、`DesktopWindow.cpp/.h`。

5. **系统级只读热键**
   - 使用 `RegisterHotKey(MOD_CONTROL|MOD_SHIFT, 'O')` 注册 Ctrl+Shift+O，窗口模式与全屏模式均有效；替代原先不可靠的 Alt+O（Alt 会被系统菜单抢占）与全局钩子方案。
   - 涉及文件：`ViewerWindow.cpp/.h`。

## 三、双向文件传输相关修改

- 新增本地/远端文件互传、拖放发送、Ctrl+Alt+G 取回、同名自动重命名、独立进度窗口与按日期日志等。
- 涉及文件：`HuTransfer.cpp/.h`、`FileTransferMainDialog.cpp/.h`、`UploadOperation.cpp/.h`、`DownloadOperation.cpp/.h`、`HuRenameSupport.h`、`CopyFileEventListener.h`、`RfbKeySym.h` 等。

## 四、未修改的第三方组件

- `src/zlib`（zlib 许可证）、`src/libjpeg`（IJG 许可证）按上游原样保留，未修改，各自许可文件随源码提供。

---

如对具体 diff 感兴趣，可按上述文件逐一与官方 2.8.90 GPL 源码对照；所有改动点均有 `HU patch` 注释标识。
