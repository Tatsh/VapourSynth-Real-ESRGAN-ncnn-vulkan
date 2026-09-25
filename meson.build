project(
  'realesrgan',
  'cpp',
  default_options: ['cpp_std=c++20'],
  license: 'MIT',
  meson_version: '>=1.3.0',
  version: '0.1.0',
)

fs = import('fs')

vapoursynth_dep = dependency('vapoursynth', required: false)
if not vapoursynth_dep.found()
  vapoursynth_include = ''
  python = import('python').find_installation(required: false)
  # VapourSynth wheels ship their headers inside the Python package rather than
  # a system include directory, so ask it where they are before giving up.
  # See https://vapoursynth.com/doc/packaging.html.
  if python.found()
    probe = run_command(
      python,
      '-c',
      'import vapoursynth; print(vapoursynth.get_include())',
      check: false,
    )
    if probe.returncode() == 0
      candidate = probe.stdout().strip()
      if candidate != '' and fs.exists(candidate / 'VapourSynth4.h') and fs.exists(
        candidate / 'VSHelper4.h',
      )
        message('Found VapourSynth headers via Python: ' + candidate)
        vapoursynth_include = candidate
      endif
    endif
  endif
  # The VapourSynth v4 headers ship inside the Python wheel, so fetch them from
  # a pinned ref when no headers are installed at all.
  if vapoursynth_include == '' and get_option('fetch_vapoursynth_headers') and python.found()
    headers_ref = get_option('vapoursynth_headers_ref')
    headers_dir = meson.current_build_dir() / 'vapoursynth-headers'
    message(
      'VapourSynth headers not found; downloading VapourSynth4.h and VSHelper4.h (' + headers_ref + ').',
    )
    run_command(
      python,
      files('tools/fetch_vapoursynth_headers.py'),
      '--ref',
      headers_ref,
      '--destination',
      headers_dir,
      check: true,
    )
    vapoursynth_include = headers_dir
  endif
  if vapoursynth_include == ''
    error(
      'VapourSynth headers not found. Install the vapoursynth Python package '
      + 'or enable the fetch_vapoursynth_headers option.',
    )
  endif
  # The headers usually live outside the source and build trees, which
  # include_directories() does not accept, so pass the flag directly. MSVC
  # understands the -I spelling as well.
  vapoursynth_dep = declare_dependency(compile_args: ['-I' + vapoursynth_include])
endif

plugins_dir = get_option('plugins_dir')
if plugins_dir == ''
  plugins_dir = get_option('libdir') / 'vapoursynth'
endif

subdir('src')

install_data(
  ['LICENSE.txt', 'README.md'],
  install_dir: get_option('datadir') / 'doc' / meson.project_name(),
)

summary(
  {
    'Plugins directory': plugins_dir,
  },
  section: 'realesrgan',
)
