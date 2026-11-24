<div align="center">

<img src="pics/icon.png" width="96" alt="sniplite 图标">

# sniplite

**轻量、快速的 Windows 截图与贴图工具**

[![English](https://img.shields.io/badge/English-eaeef2?style=flat-square)](docs/README.en.md) [![简体中文](https://img.shields.io/badge/%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-1677e5?style=flat-square)](#sniplite) [![繁體中文](https://img.shields.io/badge/%E7%B9%81%E9%AB%94%E4%B8%AD%E6%96%87-eaeef2?style=flat-square)](docs/README.zh-TW.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-eaeef2?style=flat-square)](docs/README.ja.md) [![한국어](https://img.shields.io/badge/%ED%95%9C%EA%B5%AD%EC%96%B4-eaeef2?style=flat-square)](docs/README.ko.md) [![Français](https://img.shields.io/badge/Fran%C3%A7ais-eaeef2?style=flat-square)](docs/README.fr.md) [![Deutsch](https://img.shields.io/badge/Deutsch-eaeef2?style=flat-square)](docs/README.de.md) [![Español](https://img.shields.io/badge/Espa%C3%B1ol-eaeef2?style=flat-square)](docs/README.es.md) · [下载 Releases](https://github.com/jinmeng3/sniplite/releases)

[![版本](https://img.shields.io/github/v/tag/jinmeng3/sniplite?sort=date&label=%E7%89%88%E6%9C%AC&style=flat-square&color=1677e5)](https://github.com/jinmeng3/sniplite/releases) [![系统](https://img.shields.io/badge/%E7%B3%BB%E7%BB%9F-Windows%2010%20%2F%2011-3b82f6?style=flat-square)](#系统要求) [![许可证](https://img.shields.io/badge/%E8%AE%B8%E5%8F%AF%E8%AF%81-AGPL--3.0-3b82f6?style=flat-square)](LICENSE-AGPL-3.0)

<img src="pics/zh/demo-edit.png" alt="截图后直接绘制箭头、文字和标记" width="66%">
<br>
<img src="pics/zh/demo-select.png" alt="截图选区和放大镜" width="32.5%"> <img src="pics/zh/demo-pin.png" alt="置顶显示截图" width="32.5%">
<br>
<img src="pics/zh/demo-ocr.png" alt="识别文字并保留表格结构" width="66%">
<br>
<img src="pics/zh/demo-long.png" alt="滚动并拼接长截图" width="66%">

</div>

## 功能

- **截图**：识别窗口和控件，提供放大镜及像素级选区调整。
- **标注和贴图**：绘制箭头、文字、序号和马赛克，也可将截图置顶显示。
- **二维码识别**：在截图选区按 `Ctrl+Q` 识别二维码，结果留在本机并可复制。
- **公式识别**：可选 CrispEmbed 模块，使用本机 Texo-Distill GGUF 将截图选区转换为 LaTeX 并复制，在 CPU 上本地运行。
- **文字识别**：可选 OCR 模块，支持普通文字和表格识别，在本机运行。
- **长截图**：滚动页面时自动拼接内容。
- **轻量运行**：截图、画面合成和绘制等性能敏感部分使用 x86-64 汇编；主程序不链接 C 运行库。
- **隐私**：截图和识别内容在本机处理；软件不含广告。

## 下载

前往 [GitHub Releases](https://github.com/jinmeng3/sniplite/releases) 下载当前版本。v1.5.1.5 提供一个安装程序和七种便携 ZIP：

- [Windows 安装程序](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.5/sniplite_win_setup_amd64.exe)
- [二维码便携版](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.5/sniplite_1.5.1.5_portable_QR_x64.zip)
- [二维码 + OCR CPU 版](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.5/sniplite_1.5.1.5_portable_QR_OCR_CPU_x64.zip)
- [二维码 + OCR AVX2 版](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.5/sniplite_1.5.1.5_portable_QR_OCR_AVX2_x64.zip)
- [二维码 + OCR Vulkan 版](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.5/sniplite_1.5.1.5_portable_QR_OCR_Vulkan_x64.zip)
- [二维码 + OCR + FormulaNet CPU 版](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.5/sniplite_1.5.1.5_portable_QR_OCR_FormulaNet_CPU_x64.zip)
- [二维码 + OCR + FormulaNet AVX2 版](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.5/sniplite_1.5.1.5_portable_QR_OCR_FormulaNet_AVX2_x64.zip)
- [二维码 + OCR + FormulaNet Vulkan 版](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.5/sniplite_1.5.1.5_portable_QR_OCR_FormulaNet_Vulkan_x64.zip)

解压后运行 `sniplite.exe`。命令行功能由 `sniplite-cli.com` 提供；二维码 ZIP 已附带此文件，单文件便携版请从发布页另行下载，并与主程序放在同一目录。OCR 模块可在首选项的“OCR”页面下载安装或删除；便携版在更新页检查后可直接下载更新并原地替换已有程序文件，OCR 和公式模型等目录会保留。二维码助手也可在首选项的“二维码”页面单独下载安装或删除。便携版可在首选项中开启“开机启动”；快捷方式会指向当前程序位置，因此开启后不要移动或重命名程序目录。若已移动目录，请重新关闭并开启此选项以更新快捷方式。

管理员权限运行主程序时，也请以管理员权限运行命令行程序；普通权限的命令行程序不会向提权后的主程序发送命令。

CrispEmbed 公式识别模块可在首选项的“公式识别”页面从本机模型与运行库文件安装或删除。

## 系统要求

- Windows 10 或 Windows 11 x64
- 普通截图与绘图不需要额外运行库
- OCR 需要随包提供或另行安装 OCR 模块
- 公式识别需要 Texo-Distill GGUF 模型、CrispEmbed 运行库和 Microsoft Visual C++ x64 运行库。

## 默认快捷键

| 按键 | 作用 |
| --- | --- |
| **F1** | 截图 |
| **Shift+F1** | 快速截图，将鼠标所在屏幕保存为文件 |
| **F3** | 将剪贴板中的图片或文字贴到屏幕上 |
| **Shift+F3** | 隐藏或显示所有贴图 |
| **F4** | 截图并识别文字（需要 OCR 模块） |
| **Shift+F4** | 截图并复制识别出的文字 |
| **Enter** | 复制截图 |
| **Ctrl+S** | 保存截图 |
| **Ctrl+T** 或 **鼠标中键** | 将截图贴到屏幕上 |
| **Ctrl+O** | 识别截图选区中的文字 |
| **Ctrl+Shift+C** | 复制选区中的识别文字 |
| **Ctrl+Q** | 识别截图选区中的二维码 |
| **Ctrl+L** | 长截图 |
| **Esc** | 取消截图 |
| **方向键、W A S D** | 移动鼠标或选区；按住 Shift 每次移动 10 像素 |
| **滚轮** | 截图时切换窗口范围；贴图时缩放 |
| **双击贴图** | 关闭贴图 |
| **Ctrl+H（贴图窗口）** | 将当前贴图收纳到晾衣架 |

## 常见问题

### 如何识别二维码？

截图并框选二维码后，点击工具栏二维码按钮或按 `Ctrl+Q`。程序使用同目录的 `sniplite_qr.exe` 在本机识别，结果显示在消息框中，可按 `Ctrl+C` 复制。程序不会自动打开识别出的网址。目前只支持截图选区，贴图和剪贴板图片尚未接入。更多说明见[二维码识别文档](docs/qr-code-recognition.md)（[English](docs/qr-code-recognition.en.md)）。
首选项的“二维码”页面可下载安装或删除二维码助手。安装后，在截图编辑页点击二维码按钮或按 `Ctrl+Q` 识别选区；删除后按钮和快捷键不可用。

### 如何提取截图中的文字？

框选区域后点击文字识别按钮或按 `Ctrl+O`。识别结果显示在选区旁的面板中，可以编辑和复制；表格可保留列结构，也可切换为 Markdown。此功能使用用户提供的 CrispEmbed PP-OCRv6 tiny FP16 GGUF 检测、识别模型，通过从源码构建的原生 OCR 引擎在本地运行。CPU 便携包的检测和识别均使用 CPU；Vulkan 包的两个模型均优先使用 Vulkan，设备不可用或初始化、推理失败时回退 CPU，并启用计算图优化。
### 如何识别截图中的公式？

在截图编辑页框选公式，点击“∫”识别按钮即可识别并直接复制 LaTeX；复制成功后截图界面会自动退出。点击它旁边的眼睛按钮，会打开 sniplite 的 MicroTeX 原生公式预览窗口，可编辑 LaTeX 并实时查看结果。修改公式后可复制，按 `Ctrl+Enter` 也可复制源码；公式语法有误时，预览页会显示提示。点击预览时，如果当前截图或选区还没有对应的识别结果，会先识别再打开；未识别出公式时不会打开空白页。预览窗口显示在选区所在的显示器。复制前会整理模型输出空格：移除括号和运算符周围多余空格，保留词间空格。公式模块约 82 MB，使用本机 Texo-Distill GGUF 与基于 CrispEmbed 的公式引擎，在 CPU 上运行。
识别期间不会弹出进度窗口；识别失败或结果未完整生成时，不会覆盖剪贴板。纯色空白选区会直接提示未识别到公式。


### 如何截取长图？

框选区域后点击长截图按钮或按 `Ctrl+L`，再滚动页面。程序会拼接滚过的内容；完成后可复制、保存、贴图或识别文字。

### 如何收纳多个贴图？

拖动贴图时会显示“晾衣架”，可在首选项的“晾衣架”页设置为屏幕顶部或底部居中。松开鼠标将贴图放入横栏即可收纳为缩略图。也可以在贴图窗口按 `Ctrl+H`，或从右键菜单选择“收纳到晾衣架”；右键菜单的“晾衣架”可单独打开横栏。

点击缩略图恢复贴图。按住卡片拖到另一张卡片上可调整顺序；右键卡片可恢复或关闭单张贴图。点击横栏空白处后，可用方向键选择、`Home`/`End` 跳转、`Enter` 恢复、`Delete` 关闭。拖动横栏标题可移动横栏；贴图较多时，用滚轮或 `<`、`>` 按钮翻阅。关闭横栏会恢复其中全部贴图，`Shift+F3` 会一起隐藏或显示横栏和未收纳的贴图。收纳只在当前运行期间有效，退出程序时释放全部图片。

晾衣架采用紧凑横向托盘布局，默认尺寸为 480×224（随系统 DPI 缩放），一排显示四张缩略图。顶部显示收纳数量，底部集中显示操作提示、浏览范围与翻页按钮；卡片悬停或选中时高亮，下方显示原图尺寸。空状态提示拖入贴图暂存，点击缩略图即可恢复。

![晾衣架界面](pics/ui/pin-rack.png)

### 如何降低内存占用？

首选项的截屏页面提供“省内存”模式。此模式能降低内存使用，但截图速度会相应降低。

贴图缩放时，百分比提示只为文字区域分配临时绘图缓冲，不再随贴图尺寸分配整幅缓冲。默认截图模式的图形库和设备在后台初始化；初始化期间仍可通过 GDI 截图。

长截图的画布、行哈希和缩略图按已拼接内容逐步提交内存，不会提前占用最大长度所需的全部空间；结束长截图时释放这些缓冲。

### 如何更改语言或卸载？

默认使用 Windows 语言。可从系统托盘图标打开首选项更改语言。安装版可通过开始菜单 sniplite 文件夹中的卸载快捷方式移除。

## 与 PixPin 的实测对比

| 指标 | sniplite 1.4.0 | PixPin 3.6.6.0 |
| --- | --- | --- |
| 程序体积 | 主程序 291 KiB；二维码便携 ZIP 0.96 MiB | `PixPin.exe` 6.53 MiB；安装目录 190.99 MiB |
| 空闲工作集 | 35.48 MiB | 124.43 MiB |
| F1 到截图界面就绪（20 次） | 中位数 16.35 ms；P95 30.99 ms | 中位数 31.02 ms；P95 78.69 ms；最大值 695.16 ms |

<sub>数据来自 2026 年 11 月 18 日的同一台电脑；sniplite 数据来自 1.4.0 版本。空闲工作集在完成 20 次截图热键后等待 5 秒，再以 250 ms 间隔采样 20 次。界面就绪时间各测 20 次，PixPin 为 20/20 次成功。体积统计范围不同，仅供参考；软件版本、系统状态、显示驱动和采样方式都会影响结果。</sub>

## 自行编译

项目根目录的 `w64devkit` 可直接用于构建；也可使用 MinGW-w64、CMake 和 Ninja。运行 `build.bat` 编译程序，从首选项安装 OCR 与公式模块，再运行 `package-all.bat`。它生成 QR-only，以及 QR+OCR、QR+OCR+FormulaNet 各自的 CPU、Vulkan、AVX2 便携包，共七个 ZIP。CPU 版使用通用运行库；AVX2 版优先 AVX2 并回退通用 CPU；Vulkan 版按 Vulkan、AVX2、通用 CPU 的顺序回退。输出位于 `build\dist`。
文字 OCR 打包还需要 `E:\浏览器下载` 中的 `PP-OCRv6_tiny_det-f16.gguf`、`PP-OCRv6_tiny_rec-f16.gguf` 和 `crispembed-windows-x86_64-vulkan` 运行库。固定模型校验值见 `res/ocr-notice.txt`。`build.bat` 从 PP-OCRv6 源码构建通用 CPU、AVX2 CPU 和 Vulkan 优先 OCR 引擎；CPU、AVX2、Vulkan 便携包分别携带所需引擎与运行库。OCR 模块 ZIP 中的 AVX2 文件可选，旧 ZIP 仍可安装；更换模块后更新 `src/ocr_get.c` 的 ZIP 长度和文件校验值，再重新构建并发布到 `ocr-5` 标签。
`build.bat` 从 `third_party/formulanet` 构建通用 CPU、AVX2 CPU 和 Vulkan 公式引擎。CPU 版使用通用引擎；AVX2 版仅在 Windows 报告 AVX2 可用时尝试优化引擎，加载或初始化失败时回退通用 CPU；Vulkan 版依次尝试 Vulkan、AVX2、通用 CPU。Vulkan 公式图仍可由 GGML 调度器回退到 CPU 计算。
`build.bat` 使用 CMake、Ninja 和 g++ 编译 `src/microtex`，生成 `build/sniplite_microtex.dll`，并将 `third_party/microtex` 的 `res` 和 `LICENSE` 打包为 `build/microtex-assets.zip`。DLL、资源和许可均嵌入主程序，安装版及七种便携 ZIP 均自带 MicroTeX 原生预览，无需另装预览运行库。各便携包在主程序旁附带 `LICENSE-MicroTeX.txt`，资源中的字体许可一并保留。

原生渲染器还使用 TinyXML2 10.0.0，许可见 `third_party/tinyxml2/LICENSE.txt`。安装版及所有便携 ZIP 均在主程序旁提供 `LICENSE-TinyXML2.txt`。

二维码识别使用 [ZXing-C++ v3.1.1](https://github.com/zxing-cpp/zxing-cpp)，仅启用 QR Code 解码，遵循 Apache License 2.0。详细实现和限制见[二维码识别文档](docs/qr-code-recognition.md)。
OCR 与 FormulaNet 共用的原生 CPU 卷积会在微内核启用时检查 AVX2/FMA 和操作系统的 YMM 支持。通用、AVX2 和 Vulkan DLL 都能在支持的机器上使用这个汇编微内核。PP-OCRv6 检测器的原生 CPU 回退也使用汇编计算 2×2 转置卷积。

卷积汇编同时处理 8 个输出通道和 8 个位置，按 K 维展开四次，并保留每个输出的串行 FMA 累加顺序；不足 8 个位置时使用 8×4 微内核及原有点积尾部。`CRISPEMBED_CONV2D_MK=0` 可关闭微内核，`=1` 可显式启用。转置卷积保留输入通道累加顺序及最后加偏置：通用/Vulkan 构建使用 SSE2 分离乘加，AVX2 构建使用 FMA。GGML 计算图继续使用其自身算子。

原生 CPU 公式解码还使用多输出行矩阵乘向量和单查询注意力汇编；保留默认及 `CRISPEMBED_DOT_WIDE=1` 的归约顺序。`CRISPEMBED_CONVDW_FAST=1` 的深度卷积路径为 3×3、5×5 和步长 1、2 使用三向量分块汇编，步长 2 使用不越界的重叠加载；上下 padding 行也进入 SIMD，保留 tap 顺序及零权重跳过，左右边缘及其他形状保留原计算。OCR 的文本框采样、概率图阈值比较和 3×3 膨胀也使用汇编；裁剪保持逐点坐标与半值远离零取整，阈值保持严格大于比较和 NaN 为假，膨胀保持原有轮次及边界语义。这些 CPU 前后处理也服务于 GGML 图路径。

FP16 权重使用批量转换：支持 AVX/F16C 和操作系统 YMM 状态时使用汇编，否则使用 GGML 批量接口。主机权重直接读取，省去临时副本；设备内存仍通过后端接口读取。转换保留 GGML 的浮点位模式和 MXCSR 行为，包括向下取整时的零符号及 NaN 异常状态。

检测和识别图的元数据交由 GGML 分配与释放，避免外部缓冲区的额外清零；识别图缓存仍按实际元数据和后端缓冲大小计入预算。

CPU 识别图的完整 logits 在同步计算结束后直接从主机缓冲区解码，省去临时输出分配和整块读回。该只读视图在本次解码完成前有效；图的计算、CTC 比较顺序及 GPU 读回方式保持不变。

## 许可证

本项目采用 [GNU Affero 通用公共许可证 v3.0（AGPL-3.0）](LICENSE-AGPL-3.0)。公式识别使用 CrispEmbed 和 Texo-Distill GGUF，许可文件随公式模块提供。文字 OCR 使用 CrispEmbed 的 PP-OCRv6 GGUF 模型和 GGML 运行库；MIT、Apache 2.0 许可证及第三方声明随 OCR 模块提供。公式预览使用 MicroTeX，其许可见 `third_party/microtex/LICENSE` 和发布包中的 `LICENSE-MicroTeX.txt`；字体许可随内嵌预览资源保留。其他第三方组件遵循各自的许可证。
