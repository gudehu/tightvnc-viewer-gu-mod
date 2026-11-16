# 开源许可证合规说明（为什么可以这样发布）

本项目是 TightVNC 2.8.90（GPL v2）的修改版。本文说明：它遵守什么规范、该规范的要求、以及怎样发布才不违规。

## 一、遵守的规范：GNU GPL v2

- 本项目整体遵循 **GNU General Public License, version 2（GPL v2）**，与上游 TightVNC 一致。
- 许可证原文见仓库根目录 [LICENSE](LICENSE)（也可在 `src/LICENSE` 找到）。
- GPL 是"著佐权（copyleft）"许可证：允许自由使用、修改和再发布（含商业收费），但要求修改后的作品继续以同样的许可证开放，并附带源码。

来源（可自行核对）：
- GPL v2 原文：<https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>
- GNU 官方常见问答：<https://www.gnu.org/licenses/gpl-faq.html>

## 二、GPL v2 对本项目的核心要求

依据 GPL v2 正文与 GNU 官方 FAQ：

1. **发布二进制必须同时提供"对应源码"（Corresponding Source）**（GPL v2 第 3 条）。
   - 本项目在 `src/` 提供完整源码，`bin/` 提供编译好的 exe，二者配套。GNU FAQ 问题：*"Does the GPL require that source code of modified versions be posted to the public?"* —— 不强制公开张贴，但**每个拿到程序的人都必须能同时拿到源码**；GitHub 同仓发布正好满足。

2. **修改版整体必须以 GPL v2 授权，不得改为闭源**。
   - 本仓库 LICENSE 明确为 GPL v2；不得对衍生作品切换到专有许可证。

3. **保留原有的版权声明与许可证声明**（GPL v2 第 2 条 a/c）。
   - 源码文件中 GlavSoft LLC 的版权头原样保留；`src/LICENSE`、`src/zlib/LICENSE`、`src/libjpeg/README` 均保留。

4. **对所做的修改加显著声明**（GPL v2 第 2 条 b）。
   - 所有改动在源码中以 `HU patch` 注释标注，并集中记录在 [CHANGES.md](CHANGES.md)。

5. **随程序附带一份许可证副本**。
   - 仓库根目录与 `src/` 内均有 GPL v2 全文。

6. **收费是允许的，但不得剥夺上述自由**。
   - GNU FAQ：*"Does the GPL allow me to sell copies of the program for money?"* —— 允许收费；但收了钱的人同样有权获得源码并再分发。本项目免费提供。

## 三、第三方组件

- **zlib**：`src/zlib/LICENSE`（zlib 许可证，宽松，允许再分发，需保留版权声明）——已保留。
- **libjpeg（IJG）**：`src/libjpeg/README` 中 "LEGAL ISSUES" 一节载明其条款——已保留。
- 这些组件与 GPL v2 兼容，随源码一并分发即可。

## 四、怎样发布才不会违规（操作要点）

如果你要基于本仓库继续发布：

1. **源码与程序一起发布**：不要只发 exe。要么同仓提供源码，要么提供有效的书面源码要约（GPL v2 第 3 条允许书面要约，但同仓发布最简单可靠）。
2. **保持 GPL v2**：整个衍生作品继续用 GPL v2，不要加"仅供非商业使用""禁止再分发"之类额外限制——这类条款与 GPL 冲突。
3. **保留版权与许可声明**：不要删除源码头部版权注释，不要删除 LICENSE 文件。
4. **标注你的修改**：在你改动的文件里加修改说明/日期，并更新 CHANGES.md。
5. **附带许可证**：确保分发物里始终有一份 GPL v2 全文。
6. **谨慎处理商标**：TightVNC 是 GlavSoft LLC 的商标。本修改版使用了不同的项目名（tightVNC-gu mod），并明确标注为修改版、非官方，避免误导为原厂产品。发布时应延续这一点：不要声称是官方版本，不要使用易与原厂混淆的标识。
7. **合法用途声明**：远程控制工具可能被滥用，请在说明中要求用户仅用于自己拥有或已授权的设备。

做到以上几点，即符合 GPL v2 的发布要求。

## 五、本仓库的合规自检

| 要求 | 本仓库状态 |
| --- | --- |
| 对应源码 | ✅ `src/` 完整源码，1270 个文件 |
| 二进制 | ✅ `bin/tvnviewer.exe`（与源码匹配） |
| 许可证文本 | ✅ 根目录与 src 内均有 GPL v2 全文 |
| 修改声明 | ✅ 源码 `HU patch` 注释 + CHANGES.md |
| 版权声明保留 | ✅ 上游版权头原样保留 |
| 第三方许可 | ✅ zlib / libjpeg 许可文件保留 |
| 明确标注为修改版 | ✅ 项目名与 README 均说明非官方 |
| 额外限制条款 | ✅ 无（不添加与 GPL 冲突的限制） |
