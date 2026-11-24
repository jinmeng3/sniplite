# AGENTS.md

## Project layout

- SnipLite targets Windows 10/11 x64.
- The main GUI, CLI, and installer are mostly C in `src/app/`; capture and presentation assembly is in `src/overlay/`. OCR, formula inference, scrolling, CPU kernels, and shared pixel operations live in `src/ocr/`, `src/formula/`, `src/scroll/`, `src/inference/`, and `src/pixels/`. Shared assembly includes remain in `src/`. The executables use a custom startup and link without the C runtime.
- Localized strings live in `src/lang_text.h` and `src/app/lang.*`. Keep user-visible text consistent across supported languages.
- `src/qr/` builds the QR-only helper with ZXing-C++; `src/microtex/` builds the native formula renderer. OCR and FormulaNet DLLs use wrappers under `src/ocr/` and sources in `third_party/ppocrv6/` and `third_party/formulanet/`.
- Windows resources and installer resources live in `res/`. Markdown documentation is in the root README and `docs/`; referenced screenshots live in `pics/`.

## Build and package

- Use the project-local `w64devkit` when present, or MinGW-w64 on `PATH`. Building also requires CMake and Ninja.
- `build.bat` builds the GUI, CLI, installer, QR helper, MicroTeX renderer, and generic CPU, AVX2, and Vulkan-preferred OCR/FormulaNet DLLs. Outputs go under `build/`.
- `package-all.bat` runs after `build.bat` and creates the installer, OCR module ZIP, and seven portable ZIPs in `build/dist/`. It requires both PP-OCRv6 GGUF files and the matching CrispEmbed Vulkan runtime. The script searches the root of drive `E:` for the models and expects the runtime beside them. It also builds the pinned CrispEmbed CPU runtimes through `build-cpu-runtime.bat`, which requires Git, CMake, and Ninja.
- Packaging recreates staging directories such as `build/ocr`, `build/ocr-vulkan-runtime`, and `build/dist`; inspect or preserve any local outputs needed before running it. Use an individual `package-*.bat` script when only one package is needed and its prerequisites are already staged.

## Verification

- There is no checked-in, unified test runner. `tests/` is ignored and contains local regression and benchmark tools; inspect `tests/README.md` when that directory is available, and run the narrow test relevant to the change.
- For source changes, use `build.bat` and the relevant local regression tool when available. For packaging changes, verify the affected ZIP contents and runtime selection.
- Tests and packaging can create or replace files under `build/`. Do not clean `build/`, `w64devkit/`, `tests/`, or `*.ini` wholesale; they may contain local tools, user configuration, models, or benchmark evidence.

## Change boundaries

- Git commit messages must be written in Chinese.
- Keep edits to vendored code under `third_party/` narrowly scoped; preserve upstream licensing and notices.
- Preserve the C ABI and export definitions used by the native OCR and FormulaNet DLLs. Backend selection and CPU fallback behavior are user-visible; verify changes against the targeted CPU, AVX2, or Vulkan path.
- When changing README screenshots, keep image paths valid and update the relevant localized descriptions.
