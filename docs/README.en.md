<div align="center">

<img src="../pics/icon.png" width="96" alt="sniplite icon">

# sniplite

**A lightweight, fast screenshot and pinning tool for Windows**

[简体中文](../README.md) · [GitHub Releases](https://github.com/jinmeng3/sniplite/releases)

[![Version](https://img.shields.io/github/v/tag/jinmeng3/sniplite?sort=date&label=Version&style=flat-square&color=1677e5)](https://github.com/jinmeng3/sniplite/releases) [![System](https://img.shields.io/badge/System-Windows%2010%20%2F%2011-3b82f6?style=flat-square)](#system-requirements) [![License](https://img.shields.io/badge/License-MIT-3b82f6?style=flat-square)](../LICENSE)

<img src="../pics/zh/demo-edit.png" alt="Annotate a screenshot with arrows, text, and marks" width="66%">
<br>
<img src="../pics/zh/demo-select.png" alt="Screenshot selection and magnifier" width="32.5%"> <img src="../pics/zh/demo-pin.png" alt="Keep a screenshot on top of other windows" width="32.5%">
<br>
<img src="../pics/zh/demo-ocr.png" alt="Recognize text while preserving table structure" width="66%">
<br>
<img src="../pics/zh/demo-long.png" alt="Scroll and stitch a long screenshot" width="66%">

</div>

## Features

- **Screenshots:** Detect windows and controls, with a magnifier and pixel-level selection adjustment.
- **Annotations and pins:** Draw arrows, text, numbers, and mosaics; keep screenshots above other windows.
- **QR recognition:** Press `Ctrl+Q` inside a screenshot selection. Results stay on your PC and can be copied.
- **Text recognition:** An optional OCR module recognizes text and tables locally.
- **Scrolling screenshots:** Scroll a page and automatically stitch the captured content.
- **Lightweight:** Performance-sensitive capture, composition, and drawing code uses x86-64 assembly. The main program does not link the C runtime.
- **Privacy:** Screenshots and recognition are processed locally. The app contains no ads.

## Download

Get the current version from [GitHub Releases](https://github.com/jinmeng3/sniplite/releases). Version 1.4.1 provides these QR portable packages:

- [QR portable package](https://github.com/jinmeng3/sniplite/releases/download/v1.4.1/sniplite_1.4.1_portable_QR_x64.zip): the app, QR helper, and its license.
- [QR + OCR portable package](https://github.com/jinmeng3/sniplite/releases/download/v1.4.1/sniplite_1.4.1_portable_QR_OCR_x64.zip): includes the OCR module as well as QR recognition.

Extract the ZIP and run `sniplite.exe`. QR recognition requires `sniplite_qr.exe` to remain in the same directory as the main program. Portable builds can enable “Start with Windows” in Preferences. The shortcut points to the current program location, so do not move or rename its directory afterward. If you move it, turn the option off and on again to update the shortcut.

## System requirements

- Windows 10 or Windows 11, x64
- No additional runtime is required for screenshots and annotations.
- OCR requires the included or separately installed OCR module.

## Default shortcuts

| Key | Action |
| --- | --- |
| **F1** | Take a screenshot |
| **Shift+F1** | Quick capture: save the screen under the pointer to a file |
| **F3** | Pin an image or text from the clipboard |
| **Shift+F3** | Hide or show all pins |
| **F4** | Capture and recognize text (OCR module required) |
| **Shift+F4** | Capture and copy recognized text |
| **Enter** | Copy the screenshot |
| **Ctrl+S** | Save the screenshot |
| **Ctrl+T** or **middle click** | Pin the screenshot |
| **Ctrl+O** | Recognize text in the screenshot selection |
| **Ctrl+Shift+C** | Copy recognized text from the selection |
| **Ctrl+Q** | Recognize QR codes in the screenshot selection |
| **Ctrl+L** | Scrolling screenshot |
| **Esc** | Cancel capture |
| **Arrow keys, W A S D** | Move the pointer or selection; hold Shift to move 10 pixels at a time |
| **Mouse wheel** | Change the window target while capturing; zoom a pin |
| **Double-click a pin** | Close it |

## FAQ

### How do I scan a QR code?

Capture and select the area containing the code, then click the QR toolbar button or press `Ctrl+Q`. The app uses `sniplite_qr.exe` from the same directory to decode it locally. Results appear in a message box and can be copied with `Ctrl+C`. The app does not open recognized URLs automatically. Only screenshot selections are supported; pins and clipboard images are not connected yet. See the [QR recognition guide](qr-code-recognition.en.md) or its [Simplified Chinese version](qr-code-recognition.md).

### How do I extract text from a screenshot?

Select an area and click the text recognition button or press `Ctrl+O`. Results appear in a panel beside the selection and can be edited and copied. Tables can preserve their columns or be converted to Markdown. This requires the OCR module, which uses PaddleOCR PP-OCRv6 and ONNX Runtime and processes images locally.

### How do I take a scrolling screenshot?

Select an area, click the scrolling screenshot button or press `Ctrl+L`, then scroll the page. The app stitches the content as you scroll. You can copy, save, pin, or recognize text in the result.

### How can I reduce memory use?

The capture page in Preferences has a “Low memory” mode. It reduces memory use at the cost of slower capture.

### How do I change the language or uninstall?

The app follows the Windows language by default. Open Preferences from the system tray to choose another language. To remove an installed copy, use the uninstall shortcut in the sniplite Start menu folder.

## Comparison with PixPin

| Metric | sniplite 1.4.0 | PixPin 3.6.6.0 |
| --- | --- | --- |
| Program size | Main program 291 KiB; QR portable ZIP 0.96 MiB | `PixPin.exe` 6.53 MiB; installation directory 190.99 MiB |
| Idle working set | 35.48 MiB | 124.43 MiB |
| F1 to capture UI ready (20 runs) | Median 16.35 ms; P95 30.99 ms | Median 31.02 ms; P95 78.69 ms; max 695.16 ms |

<sub>Measurements were taken on the same PC on November 18, 2026; sniplite measurements are from version 1.4.0. Idle working set was sampled 20 times at 250 ms intervals, five seconds after 20 hotkey activations. UI readiness was measured 20 times; PixPin succeeded 20/20 times. Package sizes cover different items and are provided for reference only. Results vary with software versions, system state, display drivers, and measurement methods.</sub>

## Build from source

The project-local `w64devkit` can build the project directly. Alternatively, install MinGW-w64, CMake, and Ninja. Run `build.bat` to build the programs. Run `pack-qr.bat` and `pack-qr-ocr.bat` to create the corresponding QR portable ZIPs. Output is placed in the `build` directory.

QR recognition uses [ZXing-C++ v3.1.1](https://github.com/zxing-cpp/zxing-cpp), with only QR Code decoding enabled, under Apache License 2.0. See the [QR recognition guide](qr-code-recognition.en.md) for implementation details and limitations.

## License

This project is licensed under the [MIT License](../LICENSE). Third-party components are distributed under their respective licenses. See the [upstream OCR release notes](https://github.com/MrTangLuyao/sniplite/releases/tag/ocr-1) for OCR component and model licensing details.
