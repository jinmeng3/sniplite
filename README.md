<div align="center">

<img src="pics/icon.png" width="96" alt="sniplite 图标">

# sniplite

**轻量、快速的 Windows 截图与贴图工具**

[![English](https://img.shields.io/badge/English-eaeef2?style=flat-square)](docs/README.en.md) [![简体中文](https://img.shields.io/badge/%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-1677e5?style=flat-square)](#sniplite) [![繁體中文](https://img.shields.io/badge/%E7%B9%81%E9%AB%94%E4%B8%AD%E6%96%87-eaeef2?style=flat-square)](docs/README.zh-TW.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-eaeef2?style=flat-square)](docs/README.ja.md) [![한국어](https://img.shields.io/badge/%ED%95%9C%EA%B5%AD%EC%96%B4-eaeef2?style=flat-square)](docs/README.ko.md) [![Français](https://img.shields.io/badge/Fran%C3%A7ais-eaeef2?style=flat-square)](docs/README.fr.md) [![Deutsch](https://img.shields.io/badge/Deutsch-eaeef2?style=flat-square)](docs/README.de.md) [![Español](https://img.shields.io/badge/Espa%C3%B1ol-eaeef2?style=flat-square)](docs/README.es.md) · [下载 Releases](https://github.com/jinmeng3/sniplite/releases)

[![版本](https://img.shields.io/github/v/tag/jinmeng3/sniplite?sort=date&label=%E7%89%88%E6%9C%AC&style=flat-square&color=1677e5)](https://github.com/jinmeng3/sniplite/releases) [![系统](https://img.shields.io/badge/%E7%B3%BB%E7%BB%9F-Windows%2010%20%2F%2011-3b82f6?style=flat-square)](#系统要求) [![许可证](https://img.shields.io/badge/%E8%AE%B8%E5%8F%AF%E8%AF%81-MIT-3b82f6?style=flat-square)](LICENSE)

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
- **文字识别**：可选 OCR 模块，支持普通文字和表格识别，在本机运行。
- **长截图**：滚动页面时自动拼接内容。
- **轻量运行**：截图、画面合成和绘制等性能敏感部分使用 x86-64 汇编；主程序不链接 C 运行库。
- **隐私**：截图和识别内容在本机处理；软件不含广告。

## 下载

前往 [GitHub Releases](https://github.com/jinmeng3/sniplite/releases) 下载当前版本。v1.4.1 提供以下二维码便携包：

- [二维码便携版](https://github.com/jinmeng3/sniplite/releases/download/v1.4.1/sniplite_1.4.1_portable_QR_x64.zip)：主程序、二维码识别程序及其许可证。
- [二维码 + OCR 便携版](https://github.com/jinmeng3/sniplite/releases/download/v1.4.1/sniplite_1.4.1_portable_QR_OCR_x64.zip)：在二维码便携版基础上附带 OCR 模块。

解压后运行 `sniplite.exe`。二维码识别需要将 `sniplite_qr.exe` 与主程序放在同一目录。便携版的“开机启动”选项不可用；需要开机启动时请使用安装版。

## 系统要求

- Windows 10 或 Windows 11 x64
- 普通截图与绘图不需要额外运行库
- OCR 需要随包提供或另行安装 OCR 模块

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

## 常见问题

### 如何识别二维码？

截图并框选二维码后，点击工具栏二维码按钮或按 `Ctrl+Q`。程序使用同目录的 `sniplite_qr.exe` 在本机识别，结果显示在消息框中，可按 `Ctrl+C` 复制。程序不会自动打开识别出的网址。目前只支持截图选区，贴图和剪贴板图片尚未接入。更多说明见[二维码识别文档](docs/qr-code-recognition.md)（[English](docs/qr-code-recognition.en.md)）。

### 如何提取截图中的文字？

框选区域后点击文字识别按钮或按 `Ctrl+O`。识别结果显示在选区旁的面板中，可以编辑和复制；表格可保留列结构，也可切换为 Markdown。此功能需要 OCR 模块，使用 PaddleOCR PP-OCRv6 模型和 ONNX Runtime，识别过程在本机完成。

### 如何截取长图？

框选区域后点击长截图按钮或按 `Ctrl+L`，再滚动页面。程序会拼接滚过的内容；完成后可复制、保存、贴图或识别文字。

### 如何降低内存占用？

首选项的截屏页面提供“省内存”模式。此模式能降低内存使用，但截图速度会相应降低。

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

项目根目录的 `w64devkit` 可直接用于构建；也可使用 MinGW-w64、CMake 和 Ninja。运行 `build.bat` 编译程序；运行 `pack-qr.bat` 和 `pack-qr-ocr.bat` 可生成对应的二维码便携 ZIP。输出位于 `build` 文件夹。

二维码识别使用 [ZXing-C++ v3.1.1](https://github.com/zxing-cpp/zxing-cpp)，仅启用 QR Code 解码，遵循 Apache License 2.0。详细实现和限制见[二维码识别文档](docs/qr-code-recognition.md)。

## 许可证

本项目采用 [MIT 许可证](LICENSE)。第三方组件各自遵循随程序包提供的许可证。OCR 组件和模型的许可信息见[上游 OCR 发布说明](https://github.com/MrTangLuyao/sniplite/releases/tag/ocr-1)。
