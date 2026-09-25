<!-- markdownlint-configure-file {"MD024": { "siblings_only": true } } -->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.1/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [unreleased]

## [0.1.0] - 2026-09-25

### Added

- VapourSynth v4 filter (`core.esrgan.RealESRGAN`) with ncnn Vulkan
  upscaling, model selection, scaling, tiling, and TTA options.
- Python wheel distribution installing the native plugin into
  `vapoursynth/plugins`, with one wheel per OS and CPU combination
  built in CI.
- Meson build with automatic VapourSynth header download.

### Changed

- Ported the filter from the VapourSynth v3 API to v4, requiring
  VapourSynth R66 or later.
- Replaced the CMake build with Meson.

### Removed

- Standalone application sources and the libwebp submodule.
- Legacy standalone CI workflows and sample images.

[unreleased]: https://github.com/Tatsh/vapoursynth-real-esrgan-ncnn-vulkan/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Tatsh/vapoursynth-real-esrgan-ncnn-vulkan/releases/tag/v0.1.0
