<div align="center">

<img src="pics/icon.png" width="96" alt="SnipLite 图标">

# SnipLite

**轻量、快速、本地运行的 Windows 截图与贴图工具**

[![English](https://img.shields.io/badge/English-eaeef2?style=flat-square)](docs/README.en.md) [![简体中文](https://img.shields.io/badge/%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-1677e5?style=flat-square)](#sniplite) [![繁體中文](https://img.shields.io/badge/%E7%B9%81%E9%AB%94%E4%B8%AD%E6%96%87-eaeef2?style=flat-square)](docs/README.zh-TW.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-eaeef2?style=flat-square)](docs/README.ja.md) [![한국어](https://img.shields.io/badge/%ED%95%9C%EA%B5%AD%EC%96%B4-eaeef2?style=flat-square)](docs/README.ko.md) [![Français](https://img.shields.io/badge/Fran%C3%A7ais-eaeef2?style=flat-square)](docs/README.fr.md) [![Deutsch](https://img.shields.io/badge/Deutsch-eaeef2?style=flat-square)](docs/README.de.md) [![Español](https://img.shields.io/badge/Espa%C3%B1ol-eaeef2?style=flat-square)](docs/README.es.md) · [下载 Releases](https://github.com/jinmeng3/sniplite/releases)

[![版本](https://img.shields.io/github/v/tag/jinmeng3/sniplite?sort=date&label=%E7%89%88%E6%9C%AC&style=flat-square&color=1677e5)](https://github.com/jinmeng3/sniplite/releases) [![系统](https://img.shields.io/badge/%E7%B3%BB%E7%BB%9F-Windows%2010%20%2F%2011-3b82f6?style=flat-square)](#系统要求) [![许可证](https://img.shields.io/badge/%E8%AE%B8%E5%8F%AF%E8%AF%81-AGPL--3.0-3b82f6?style=flat-square)](LICENSE-AGPL-3.0)

</div>

SnipLite 将截图、标注、贴图、长截图、二维码识别和本地 OCR 放在一个小巧的 Windows 工具中。可选的公式识别模块使用本机 GGUF 模型；识别内容留在本机处理。

<p align="center">
  <img src="pics/zh/demo-select.png" width="49%" alt="窗口识别、放大镜与像素级选区">
  <img src="pics/zh/demo-edit.png" width="49%" alt="截图标注工具栏与颜色取样放大镜">
</p>
<p align="center">
  <img src="pics/zh/demo-ocr.png" width="49%" alt="OCR 识别文字并保留表格结构">
  <img src="pics/zh/demo-pin.png" width="49%" alt="桌面贴图和标注">
</p>
<p align="center">
  <img src="pics/zh/demo-long.png" width="75%" alt="选取要拼接的滚动页面">
</p>

## 功能一览

- **快速截图**：识别窗口和控件，使用放大镜精确选区；支持 DXGI 快速模式和 GDI 省内存模式。
- **标注**：绘制矩形、椭圆、线条、箭头、手绘线、文字、序号和马赛克。
- **桌面贴图**：把截图或剪贴板内容置顶显示，可缩放、调节透明度，并收纳到晾衣架。
- **长截图**：滚动网页或文档时自动拼接内容，完成后可复制、保存、贴图或识别文字。
- **二维码识别**：在截图选区按 `Ctrl+Q` 识别二维码；结果可复制，程序不会自动打开识别出的网址。
- **文字识别**：本地 PP-OCRv6 tiny 模型识别普通文字和表格；表格可复制为表格或 Markdown。
- **公式识别与预览**：将截图中的公式识别为 LaTeX；MicroTeX 原生预览支持编辑、实时渲染和语法错误提示。
- **本地处理**：截图和识别内容在本机处理；软件不含广告。
- **轻量运行**：主程序不链接 C 运行库；截图、合成和绘制等性能敏感部分使用 x86-64 汇编。

## 最近的 OCR 与公式引擎改进

OCR 和 FormulaNet 均可使用 Vulkan、AVX2 或通用 CPU。自动模式优先尝试 Vulkan，再尝试 AVX2 和通用 CPU；加速后端初始化或推理失败时会回退。也可在首选项中指定后端。

FormulaNet 的 Vulkan 路径会把卷积权重转换为 F16 并常驻 Vulkan 设备内存，后续推理复用缓存，避免反复转换和上传。Vulkan 图中的算子仍由 GGML 调度器分配，部分算子可在 CPU 上执行。

同一台 Windows 电脑上的七种便携包基准。OCR 与 FormulaNet 各场景预热 1 次，再分别用 5 个独立进程运行；表中速度为 wall-time 中位数。

| 便携包 | QR (ms; 工作集/提交 MiB) | OCR 单行 (ms; 工作集/提交 MiB) | FormulaNet (ms; 工作集/提交 MiB) |
| --- | ---: | ---: | ---: |
| QR-only | 34.5; 9.8/2.1 | — | — |
| QR + OCR CPU | 30.8; 9.8/2.1 | 315.5; 156.1/305.3 | — |
| QR + OCR AVX2 | 30.7; 9.8/2.1 | 237.2; 156.2/305.3 | — |
| QR + OCR Vulkan | 30.5; 9.8/2.1 | 295.9; 84.7/390.2 | — |
| QR + OCR + FormulaNet CPU | 31.7; 9.8/2.1 | 363.8; 156.1/305.3 | 1,428.5; 128.8/288.9 |
| QR + OCR + FormulaNet AVX2 | 32.0; 9.8/2.1 | 249.4; 156.2/305.3 | 477.5; 128.9/288.9 |
| QR + OCR + FormulaNet Vulkan | 31.3; 9.8/2.1 | 343.2; 84.7/390.2 | 386.9; 96.7/351.3 |

QR 使用同一张测试码；OCR 使用 1000×260 RGB 单行文本；FormulaNet 使用 400×100 的 `x² + y² = 1`。速度包括进程启动、模型初始化、预处理和推理。内存为进程峰值工作集/峰值提交量，使用 Windows PSAPI 每 2 ms 采样；不含专用显存，OCR 峰值包含检测与识别模型。显卡、驱动和系统状态会影响结果。

## 下载与安装

前往 [GitHub Releases](https://github.com/jinmeng3/sniplite/releases) 下载当前版本。v1.5.1.6 提供安装程序和七种便携 ZIP：

- [Windows 安装程序](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.6/sniplite_win_setup_amd64.exe)
- [二维码便携版](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.6/sniplite_1.5.1.6_portable_QR_x64.zip)
- 二维码 + OCR： [CPU](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.6/sniplite_1.5.1.6_portable_QR_OCR_CPU_x64.zip) · [AVX2](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.6/sniplite_1.5.1.6_portable_QR_OCR_AVX2_x64.zip) · [Vulkan](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.6/sniplite_1.5.1.6_portable_QR_OCR_Vulkan_x64.zip)
- 二维码 + OCR + FormulaNet： [CPU](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.6/sniplite_1.5.1.6_portable_QR_OCR_FormulaNet_CPU_x64.zip) · [AVX2](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.6/sniplite_1.5.1.6_portable_QR_OCR_FormulaNet_AVX2_x64.zip) · [Vulkan](https://github.com/jinmeng3/sniplite/releases/download/v1.5.1.6/sniplite_1.5.1.6_portable_QR_OCR_FormulaNet_Vulkan_x64.zip)

便携版解压后运行 `sniplite.exe`。二维码 ZIP 已包含命令行程序 `sniplite-cli.com`。OCR 和公式识别模块可在首选项对应页面安装或删除；FormulaNet 模块也可从本机模型与运行库文件安装。安装版和便携版均包含 MicroTeX 原生公式预览。

便携版支持在首选项开启开机启动。开启后快捷方式会指向当前程序位置；移动或重命名程序目录后，请重新关闭并开启此选项。

## 默认快捷键

| 按键 | 作用 |
| --- | --- |
| `F1` | 截图 |
| `Shift+F1` | 快速截图并保存鼠标所在屏幕 |
| `F3` | 将剪贴板图片或文字贴到屏幕上 |
| `Shift+F3` | 隐藏或显示所有贴图 |
| `F4` | 截图并识别文字（需要 OCR 模块） |
| `Shift+F4` | 截图并复制识别出的文字 |
| `Enter` | 复制截图 |
| `Ctrl+S` | 保存截图 |
| `Ctrl+T` 或鼠标中键 | 将截图贴到屏幕上 |
| `Ctrl+O` | 识别截图选区中的文字 |
| `Ctrl+Shift+C` | 复制选区中的识别文字 |
| `Ctrl+Q` | 识别截图选区中的二维码 |
| `Ctrl+L` | 长截图 |
| `Esc` | 取消截图 |
| 方向键或 `W` `A` `S` `D` | 移动鼠标或选区；按住 `Shift` 每次移动 10 像素 |
| 滚轮 | 截图时切换窗口范围；贴图时缩放 |
| 双击贴图 | 关闭贴图 |
| 贴图窗口中的 `Ctrl+H` | 将贴图收纳到晾衣架 |

## OCR 与公式识别

### OCR

框选文字后点击工具栏识别按钮或按 `Ctrl+O`。识别结果显示在选区旁，可编辑和复制；表格可保留列结构，也可切换为 Markdown。OCR 使用 CrispEmbed PP-OCRv6 tiny FP16 GGUF 检测与识别模型，由从源码构建的本机引擎运行。

### 公式识别

在截图编辑页框选公式并点击 `∫` 按钮，识别结果会以 LaTeX 复制到剪贴板。点击旁边的预览按钮可打开 MicroTeX 窗口编辑 LaTeX 并实时查看渲染；按 `Ctrl+Enter` 复制源码。识别期间不会显示进度窗口；失败或结果未完整生成时不会覆盖剪贴板。

公式识别由本机 Texo-Distill GGUF 模型和 CrispEmbed 引擎运行。首选项可选择自动、Vulkan、AVX2 或 CPU；加速后端失败时会回退。公式预览直接内嵌 MicroTeX，无需额外安装预览运行库。单独安装公式模块时，系统还需 Microsoft Visual C++ x64 运行库。

## 晾衣架

贴图可拖到屏幕边缘的绳线上暂存。晾衣架平时收起在首选项指定的顶部或底部边缘；鼠标停在该边缘约 300 毫秒后滑出，离开卡片后约 500 毫秒收回。也可从贴图窗口按 `Ctrl+H` 收纳当前贴图，或从贴图菜单手动打开晾衣架。点击缩略图恢复贴图；拖动卡片可调整顺序，右键菜单可恢复或关闭单张贴图。架子以透明悬浮层呈现横跨显示器的弧形绳线，贴图按原图比例缩放为夹挂卡片；卡片显示原图尺寸，翻页与关闭控件位于卡片另一侧。

<p align="center">
  <img src="pics/ui/pin-rack.png" width="72%" alt="横向晾衣架中的贴图缩略图、状态和翻页控件">
</p>

晾衣架中的贴图只在当前运行期间保留；退出 SnipLite 时会释放。

## 系统要求

- Windows 10 或 Windows 11，x64。
- 截图和绘图无需额外运行库。
- OCR 和公式识别需要对应模块；专用便携包已预置相应组件，二维码便携版可从首选项安装。Vulkan、AVX2 后端取决于设备支持情况，CPU 后端可作为回退。
- 管理员权限运行主程序时，也请以管理员权限运行命令行程序。

## 自行编译

项目根目录的 `w64devkit` 可用于构建；也可使用 MinGW-w64、CMake 和 Ninja。运行 `build.bat` 编译，再运行 `package-all.bat` 生成便携包。脚本会生成 QR-only，以及 QR+OCR、QR+OCR+FormulaNet 各自的 CPU、Vulkan、AVX2 包，共七种 ZIP。完整打包 OCR 和公式引擎还需要对应的 GGUF 模型及 CrispEmbed 运行库。

`build.bat` 从 PP-OCRv6 和 FormulaNet 源码构建 CPU、AVX2 与 Vulkan 引擎，并构建内嵌的 MicroTeX 公式预览。便携包包含相应第三方许可文件。二维码识别使用 [ZXing-C++](https://github.com/zxing-cpp/zxing-cpp)，仅启用 QR Code 解码。

OCR 与 FormulaNet 共用的原生 CPU 卷积会在微内核启用时检查 AVX2/FMA 和操作系统的 YMM 支持。通用、AVX2 和 Vulkan DLL 都能在支持的机器上使用这个汇编微内核。PP-OCRv6 检测器的原生 CPU 回退也使用汇编计算 2×2 转置卷积。

卷积汇编同时处理 8 个输出通道和 8 个位置，按 K 维展开四次，并保留每个输出的串行 FMA 累加顺序；不足 8 个位置时使用 8×4 微内核及原有点积尾部。`CRISPEMBED_CONV2D_MK=0` 可关闭微内核，`=1` 可显式启用。转置卷积保留输入通道累加顺序及最后加偏置：通用/Vulkan 构建使用 SSE2 分离乘加，AVX2 构建使用 FMA。GGML 计算图继续使用其自身算子。

原生 CPU 公式解码还使用多输出行矩阵乘向量和单查询注意力汇编；保留默认及 `CRISPEMBED_DOT_WIDE=1` 的归约顺序。`CRISPEMBED_CONVDW_FAST=1` 的深度卷积路径为 3×3、5×5 和步长 1、2 使用三向量分块汇编，步长 2 使用不越界的重叠加载；上下 padding 行也进入 SIMD，保留 tap 顺序及零权重跳过，左右边缘及其他形状保留原计算。OCR 的文本框采样、概率图阈值比较和 3×3 膨胀也使用汇编；裁剪保持逐点坐标与半值远离零取整，阈值保持严格大于比较和 NaN 为假，膨胀保持原有轮次及边界语义。这些 CPU 前后处理也服务于 GGML 图路径。

FP16 权重使用批量转换：支持 AVX/F16C 和操作系统 YMM 状态时使用汇编，否则使用 GGML 批量接口。主机权重直接读取，省去临时副本；设备内存仍通过后端接口读取。转换保留 GGML 的浮点位模式和 MXCSR 行为，包括向下取整时的零符号及 NaN 异常状态。

检测和识别图的元数据交由 GGML 分配与释放，避免外部缓冲区的额外清零；识别图缓存仍按实际元数据和后端缓冲大小计入预算。

CPU 识别图的完整 logits 在同步计算结束后直接从主机缓冲区解码，省去临时输出分配和整块读回。该只读视图在本次解码完成前有效；图的计算、CTC 比较顺序及 GPU 读回方式保持不变。

## 许可证

SnipLite 使用 [GNU Affero 通用公共许可证 v3.0（AGPL-3.0）](LICENSE-AGPL-3.0)。公式识别、OCR、MicroTeX 和其他第三方组件遵循各自许可；相关许可文件随源码或对应模块提供。
