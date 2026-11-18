# sniplite 二维码识别

## 当前实现

二维码识别在本机运行，不上传图片或识别内容。截图选区工具栏提供二维码按钮，快捷键为 `Ctrl+Q`；结果显示在消息框中，可用 `Ctrl+C` 复制，不会自动打开 URL。当前接入截图选区，贴图菜单和剪贴板图片尚未接入。最多处理 16,777,216 像素。

解码器使用固定版本的 [ZXing-C++ v3.1.1](https://github.com/zxing-cpp/zxing-cpp)，源码位于 `third_party/zxing-cpp/core/`，许可证为 Apache 2.0，随发布包提供 `LICENSE-ZXing.txt`。构建只启用 QR Code reader，不启用其他条码格式或编码器。独立的 `sniplite_qr.exe` 通过 Windows Imaging Component 读取 PNG、JPEG、BMP 等 Windows 支持的图像格式；选区像素通过管道传给同一 helper。主截图程序不链接或初始化 ZXing。

构建需要项目根目录的 w64devkit（内含 GCC、CMake 和 Ninja），或可用的 MinGW-w64、CMake 和 Ninja：

```powershell
.\build.bat
.\build\sniplite_qr.exe .\capture.png
```

## 解码设置

helper 将图像转为灰度后调用 ZXing C API，只搜索 QR Code。启用 try harder、90 度旋转、反色和大图降采样选项，并最多返回 32 个识别结果。多个结果逐个写入标准输出；无结果时返回退出码 4。安装器将 helper 和许可证安装在主程序旁边；便携版也需将 helper 与主程序放在同一目录。

## 当前用户流程

1. 用户截图并框选包含二维码的区域。
2. 点击工具栏二维码按钮，或按 `Ctrl+Q`。
3. 当前选区像素通过管道交给旁边的 QR helper 解码。
4. 找到的结果在消息框中显示；用户可按 `Ctrl+C` 复制消息框内容。
5. 没有识别结果时显示空结果提示。

## 边界与验证

识别结果可能包含截图中的敏感信息；处理保持本地，不写日志，不保存历史。严重模糊、极低分辨率、强透视或严重遮挡的二维码仍可能无法识别。

验证应覆盖普通网址、中文文本、反色和旋转 QR、同图多个 QR、空白图、损坏图、二维码周围有其他内容的截图、不同 DPI 和缩放比例，以及超大图尺寸限制。构建和发布时应确认 x64 helper 可独立运行，并将 Apache 2.0 许可证随安装包和便携包发布。
