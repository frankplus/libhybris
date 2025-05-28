# android versions
if android_headers_major >= 11 and android_headers_minor >= 0
  add_project_arguments('-DHAS_ANDROID_11_0_0', language : 'cpp')
endif
if android_headers_major >= 10 and android_headers_minor >= 0
  add_project_arguments('-DHAS_ANDROID_10_0_0', language : 'cpp')
  add_project_arguments('-DWANT_LINKER_Q', language : 'c')
endif
if android_headers_major >= 9 and android_headers_minor >= 0
  add_project_arguments('-DHAS_ANDROID_9_0_0', language : 'cpp')
endif
if android_headers_major >= 8 and android_headers_minor >= 0
  add_project_arguments('-DHAS_ANDROID_8_0_0', language : 'cpp')
  add_project_arguments('-DWANT_LINKER_O', language : 'c')
endif
if android_headers_major >= 7 and android_headers_minor >= 0
  add_project_arguments('-DHAS_ANDROID_7_0_0', language : 'cpp')
  add_project_arguments('-DWANT_LINKER_N', language : 'c')
endif
if android_headers_major >= 6 and android_headers_minor >= 0
  add_project_arguments('-DHAS_ANDROID_6_0_0', language : 'cpp')
  add_project_arguments('-DWANT_LINKER_MM', language : 'c')
endif
if android_headers_major >= 5 and android_headers_minor >= 0
  add_project_arguments('-DHAS_ANDROID_5_0_0', language : 'cpp')
endif
if android_headers_major >= 4 and android_headers_minor >= 2
  add_project_arguments('-DHAS_ANDROID_4_2_0', language : 'cpp')
endif
if android_headers_major >= 4 and android_headers_minor >= 1
  add_project_arguments('-DHAS_ANDROID_4_1_0', language : 'cpp')
endif

if get_option('arch') != 'arm64' and get_option('arch') != 'x86_64'
  add_project_arguments('-DWANT_LINKER_JB', language : 'c')
endif

if get_option('arm_tracing')
  add_project_arguments('-DWANT_ARM_TRACING', language : ['c', 'cpp'])
endif

common_inc = include_directories('.')

thread_dep = dependency('threads')

if android_headers_major >= 10 and android_headers_minor >= 0
  subdir('q')
endif
if android_headers_major >= 8 and android_headers_minor >= 0
  subdir('o')
endif
if android_headers_major >= 7 and android_headers_minor >= 0
  subdir('n')
endif
if android_headers_major >= 6 and android_headers_minor >= 0
  subdir('mm')
endif

if get_option('arch') != 'arm64' and get_option('arch') != 'x86_64'
  subdir('jb')
endif

if get_option('stub_linker')
  subdir('stub')
endif

libhybris_common_sources = [
  'hooks.c',
  'hooks_shm.c',
  'strlcpy.c',
  'strlcat.c',
  'logging.c',
  'native_handle.c',
  'sysconf.c',
  'dso_handle_counters.cpp',
  'legacy_properties/properties.c',
  'legacy_properties/cache.c',
]

linker_plugin_install_dir = join_paths(get_option('libdir'), 'libhybris/linker')

libhybris_common_c_args = ['-DLINKER_PLUGIN_DIR="@0@"'.format(linker_plugin_install_dir)]

if get_option('property_cache')
  libhybris_common_sources += ['legacy_properties/runtime_cache.c']
else
  libhybris_common_c_args += ['-DNO_RUNTIME_PROPERTY_CACHE']
endif

if get_option('arm_tracing')
  libhybris_common_sources += ['wrappers.c', 'wrapper_code_generic_arm.c']
endif

# For musl/OpenHarmony, dl functions are built into libc
dl_dep = cc.find_library('dl', required : false)
if not dl_dep.found()
  # Check if we're cross-compiling for musl/OpenHarmony
  if meson.is_cross_build() and get_option('arch') == 'arm64'
    # Assume dl functions are built into libc for OpenHarmony
    message('Using built-in dl functions for OpenHarmony/musl')
    dl_dep = dependency('', required : false)  # Empty dependency
  else
    dl_dep = cc.find_library('dl', required : true)  # Will fail with helpful error
  endif
endif

rt_dep = cc.find_library('rt', required : false)
if not rt_dep.found()
  # For musl/OpenHarmony, rt functions are built into libc
  if meson.is_cross_build() and get_option('arch') == 'arm64'
    # Assume rt functions are built into libc for OpenHarmony
    message('Using built-in rt functions for OpenHarmony/musl')
    rt_dep = dependency('', required : false)  # Empty dependency
  else
    rt_dep = cc.find_library('rt', required : true)  # Will fail with helpful error
  endif
endif

libhybris_common_deps = [hybris_dep, thread_dep]
if dl_dep.found()
  libhybris_common_deps += [dl_dep]
endif
if rt_dep.found()
  libhybris_common_deps += [rt_dep]
endif

libhybris_common = library('hybris-common',
  libhybris_common_sources,
  include_directories : [config_inc, header_inc],
  c_args : libhybris_common_c_args,
  dependencies : libhybris_common_deps,
  pic : true,
  version : '1.0.0',
  install : true
)

hybris_common_dep = declare_dependency(
  link_with : libhybris_common,
  include_directories : ['.'],
  dependencies : hybris_dep,
)
