# AGENTS.md

## Project overview

- SnipLite targets Windows 10/11 x64. The GUI, CLI, and installer are primarily in C under `src/app/`; screenshot capture and presentation assembly are in `src/overlay/`.
- Core components are grouped by function: OCR (`src/ocr/`), formula inference (`src/formula/`), scrolling (`src/scroll/`), inference (`src/inference/`), and pixel operations (`src/pixels/`). Shared assembly includes are under `src/`. Executables use a custom startup and link without the C runtime.
- Localized strings are defined in `src/lang_text.h` and `src/app/lang.*`. Keep user-visible text consistent across supported languages.
- `src/qr/` builds the QR-only helper with ZXing-C++; `src/microtex/` builds the native formula renderer. OCR and FormulaNet DLL wrappers are under `src/ocr/`; their implementation sources are under `third_party/ppocrv6/` and `third_party/formulanet/`.
- Windows and installer resources are under `res/`. Markdown documentation is in the root README and `docs/`; referenced screenshots are in `pics/`.
- `tests/` stores experiment and test code, results, and per-run `README.md` notes.

## Problem solving and verification

- Start by investigating the reported problem in the relevant code, logs, and configuration. If the expected behavior or reproduction conditions are unclear, ask the user focused questions before work that depends on those details.
- Reproduce the current behavior with a focused test when possible. Use the result to confirm or revise the cause hypothesis; keep investigating and testing until the cause is understood.
- Once the cause is understood, make a focused fix and rerun the reproduction or targeted test. Follow the results with further diagnosis and changes as needed until the observed behavior matches the expected behavior.
- Choose checks relevant to the change and report their outcomes. If a necessary check cannot run because a tool, dependency, or environment is unavailable, explain the blocker and do not describe the issue as verified.
- For each experiment or test run, use a separate subdirectory under `tests/` and save the code, results, and a `README.md` describing the run.
- There is no checked-in unified test runner. `tests/` is ignored and may contain local tools; inspect existing contents before changing them. For source changes, use `build.bat` and relevant local tools when available. For packaging changes, verify the affected ZIP contents and runtime selection.

## Build and packaging

- Prefer the project-local `w64devkit` when available; otherwise use MinGW-w64 from `PATH`. Building requires CMake and Ninja as well.
- Run `build.bat` to build the GUI, CLI, installer, QR helper, MicroTeX renderer, and OCR/FormulaNet DLL variants for generic CPU, AVX2, and Vulkan-preferred backends. `build/` holds only products (`*.exe`, `*.dll`, `*.com`, `LICENSE-*.txt`) plus the fixed subdirectories `build/obj` (every intermediate: `.o`, import libraries, `.res`, asset zip), `build/stage` (`translation/`, `ocr/`, `formula/`, `ocr-vulkan-runtime/`), `build/microtex`, `build/qr`, and `build/dist`.
- Run `package-all.bat` after `build.bat` to create the installer, OCR module ZIP, and seven portable ZIPs under `build/dist/`. It requires both PP-OCRv6 GGUF model files and the matching CrispEmbed Vulkan runtime. It searches the root of drive `E:` for the models and expects the runtime beside them. It also builds pinned CrispEmbed CPU runtimes through `build-cpu-runtime.bat`, which requires Git, CMake, and Ninja.
- Packaging recreates staging directories, including `build/stage/ocr`, `build/stage/ocr-vulkan-runtime`, and `build/dist`, and reads staged inputs only from `build/stage`. Inspect or preserve needed local outputs before packaging. If only one package is needed, use its `package-*.bat` script when prerequisites are already staged.
- Builds, tests, and packaging may create or replace files under `build/`. Do not wholesale clean `build/`, `w64devkit/`, `tests/`, or `*.ini`; these may contain local tools, configuration, models, or benchmark evidence.

## Change boundaries

- Write Git commit messages in Chinese.
- Keep changes to vendored code under `third_party/` narrowly scoped and preserve upstream licenses and notices.
- Preserve the C ABI and exported definitions of native OCR and FormulaNet DLLs. Backend selection and CPU fallback behavior affect users; verify changes against the relevant CPU, AVX2, or Vulkan path.
- When changing README screenshots, keep image paths valid and update the corresponding localized descriptions.

## Completion summary

- Summarize the cause, the change, and the checks performed with their results. State any unresolved issue or verification blocker.
