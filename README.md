# tcl-mupdfv

A small, fast PDF viewer plugin for Total Commander's Lister (F3) and Quick View (Ctrl+Q), built on a trimmed [MuPDF](https://mupdf.com).

It only displays PDFs: scroll, **Ctrl+mouse wheel** to zoom, drag to select text, **Ctrl+C** to copy. See [SPECS.md](SPECS.md) for details.

## Install

1. Download `tcl-mupdfv-<version>.zip` from [Releases](https://github.com/Civ01/tcl-mupdfv/releases), or a CI build (GitHub → Actions → CI → artifacts).
2. Open the zip in Total Commander and confirm the installation, or add `tcl-mupdfv.wlx64` (64-bit TC) / `tcl-mupdfv.wlx` (32-bit TC) under *Configuration → Options → Plugins → Lister (WLX)*.

Requires Windows 10 or later. The plugin is a single file.

## What it doesn't show

To stay small, some rarely needed parts of MuPDF are left out. When a document uses them, a yellow strip at the top says so; click it to hide it.

- JPEG 2000 images.
- Annotations and form fields that have no appearance of their own (most have one).
- CJK text in encodings other than the four Adobe-*-UCS2 ones.

The standard PDF fonts (Helvetica, Times, Courier) are taken from Windows (Arial, Times New Roman, Courier New).

## Build

Needs CMake ≥ 3.20 and Visual Studio 2022.

```bat
git clone https://github.com/Civ01/tcl-mupdfv
cd tcl-mupdfv
git submodule update --init --depth 1 extern/mupdf
git -C extern/mupdf submodule update --init --depth 1 thirdparty/freetype thirdparty/jbig2dec thirdparty/libjpeg thirdparty/zlib
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- **32-bit plugin:** configure with `-A Win32` to get `tcl-mupdfv.wlx`.
- **Package:** `cmake --build build --config Release --target package_zip` creates `build\tcl-mupdfv-<version>-<arch>.zip`.
- **Release:** pushing a tag `vX.Y.Z` builds both architectures and publishes a release.

## Licence

AGPL-3.0, as required by MuPDF. MuPDF is Copyright © Artifex Software, Inc.
