cpp = meson.get_compiler('cpp')
is_msvc = cpp.get_argument_syntax() == 'msvc'

vulkan_dep = dependency('vulkan', required: true)
# Prefer the CMake package config, like the old CMake build did with
# find_package(ncnn): the pkg-config file does not carry ncnn's include
# subdirectory, so quoted includes such as "net.h" fail with it.
ncnn_dep = dependency('ncnn', method: 'cmake', required: false)
if not ncnn_dep.found()
  ncnn_dep = dependency('ncnn', method: 'pkg-config', required: true)
endif

py_mod = import('python')
py = py_mod.find_installation(required: false)

# glslangValidator ships with the Vulkan SDK as well as system packages, so
# look next to a Vulkan SDK pointed at by VULKAN_SDK before giving up. This
# mirrors the old CMake build, which searched $VULKAN_SDK/bin.
glslang = find_program('glslangValidator', required: false)
if not glslang.found()
  vulkan_sdk = ''
  if py.found()
    vulkan_sdk = run_command(
      py,
      '-c',
      'import os; print(os.environ.get("VULKAN_SDK", ""), end="")',
      check: false,
    ).stdout().strip()
  endif
  if vulkan_sdk != ''
    glslang = find_program('glslangValidator', vulkan_sdk / 'bin', required: false)
  endif
  if not glslang.found()
    error(
      'glslangValidator not found. Install it (glslang-tools) or point VULKAN_SDK at a Vulkan SDK.',
    )
  endif
endif

# Mirror the old CMake compile_shader macro: each .comp shader is compiled to a
# C hex dump three ways (plain, fp16 storage, int8 storage).
shader_headers = []
foreach shader : [
  'realesrgan_preproc',
  'realesrgan_postproc',
  'realesrgan_preproc_tta',
  'realesrgan_postproc_tta',
]
  shader_headers += custom_target(
    shader + '_spv',
    input: shader + '.comp',
    output: shader + '.spv.hex.h',
    command: [glslang, '-V', '-s', '-x', '-o', '@OUTPUT@', '@INPUT@'],
  )
  shader_headers += custom_target(
    shader + '_fp16s_spv',
    input: shader + '.comp',
    output: shader + '_fp16s.spv.hex.h',
    command: [glslang, '-DNCNN_fp16_storage=1', '-V', '-s', '-x', '-o', '@OUTPUT@', '@INPUT@'],
  )
  shader_headers += custom_target(
    shader + '_int8s_spv',
    input: shader + '.comp',
    output: shader + '_int8s.spv.hex.h',
    command: [
      glslang,
      '-DNCNN_fp16_storage=1',
      '-DNCNN_int8_storage=1',
      '-V',
      '-s',
      '-x',
      '-o',
      '@OUTPUT@',
      '@INPUT@',
    ],
  )
endforeach

realesrgan_link_args = []
if not is_msvc and cpp.get_id() == 'gcc'
  # Match the old CMake build, which linked libstdc++ statically so the plugin
  # does not depend on the host's C++ runtime.
  realesrgan_link_args += '-static-libstdc++'
endif

shared_library(
  'realesrgan',
  'main.cpp',
  'realesrgan.cpp',
  shader_headers,
  dependencies: [vapoursynth_dep, vulkan_dep, ncnn_dep],
  gnu_symbol_visibility: 'hidden',
  link_args: realesrgan_link_args,
  install: true,
  install_dir: plugins_dir,
)
