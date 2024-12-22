# ─────────────────────────────────────────────────────────────
# 1. SUBDIRECTORIES
# ─────────────────────────────────────────────────────────────
# In Autotools, SUBDIRS = jb mm n o q, *if* certain conditions.
# In Meson, you just do conditional subdir() calls.

if not get_option('arch_arm64') and not get_option('arch_x86_64')
  subdir('jb')
endif

if get_option('android_6_0_0')
  subdir('mm')
endif

if get_option('android_7_0_0')
  subdir('n')
endif

if get_option('android_8_0_0')
  subdir('o')
endif

if get_option('android_10_0_0')
  subdir('q')
endif

# ─────────────────────────────────────────────────────────────
# 2. SOURCE FILES
# ─────────────────────────────────────────────────────────────
# The main set of .c / .cpp from `libhybris_common_la_SOURCES`.
common_c_sources = [
  'hooks.c',
  'hooks_shm.c',
  'strlcpy.c',
  'strlcat.c',
  'logging.c',
  'native_handle.c',
  'sysconf.c',
  'legacy_properties/properties.c',
  'legacy_properties/cache.c'
]

common_cpp_sources = [
  'dso_handle_counters.cpp'
]

# If WANT_ARM_TRACING => add wrappers.c + wrapper_code_generic_arm.c
if get_option('arm_tracing')
  common_c_sources += [
    'wrappers.c',
    'wrapper_code_generic_arm.c'
  ]
endif

# ─────────────────────────────────────────────────────────────
# 3. COMPILER & PREPROCESSOR FLAGS
# ─────────────────────────────────────────────────────────────
# In Autotools, we had libhybris_common_la_CPPFLAGS and CFLAGS, etc.

# Basic includes
common_includes = include_directories(
  '..',      # go up to top-level, if needed
  '.',
  '../include'
)

# We'll collect extra defines in an array. We can either pass them via
# add_project_arguments() or directly to shared_library(c_args/cpp_args).
common_defines = []

# If WANT_RUNTIME_PROPERTY_CACHE => add runtime_cache.c,
# else define -DNO_RUNTIME_PROPERTY_CACHE
if get_option('runtime_property_cache')
  common_c_sources += [ 'legacy_properties/runtime_cache.c' ]
else
  common_defines += ['-DNO_RUNTIME_PROPERTY_CACHE']
endif

# Reflect the old -DLINKER_PLUGIN_DIR="@libdir@/libhybris/linker"
linker_plugin_dir = '"@0@/libhybris/linker"'.format(get_option('libdir'))
common_defines += [ '-DLINKER_PLUGIN_DIR=' + linker_plugin_dir ]

# Example: If your top-level meson_options.txt includes an `android_headers_cflags`
if get_option('android_headers_cflags') != ''
  common_defines += meson.get_compiler('c').get_supported_arguments(
    get_option('android_headers_cflags').split()
  )
endif

# Now replicate the older #ifdef checks:

if get_option('adreno_quirks')
  common_defines += [ '-DWANT_ADRENO_QUIRKS' ]
endif

if get_option('trace')
  # In the original Makefile, WANT_TRACE => -DDEBUG
  common_defines += [ '-DDEBUG' ]
endif

if get_option('debug')
  # In the original Makefile, WANT_DEBUG => -ggdb -O0
  common_defines += [ '-ggdb', '-O0' ]
endif

if get_option('mali_quirks')
  common_defines += [ '-DMALI_QUIRKS' ]
endif

if get_option('ubuntu_linker_overrides')
  common_defines += [ '-DUBUNTU_LINKER_OVERRIDES' ]
endif

if not get_option('arch_arm64')
  # i.e. !WANT_ARCH_ARM64 => -DWANT_LINKER_JB
  common_defines += [ '-DWANT_LINKER_JB' ]
endif

if get_option('android_6_0_0')
  common_defines += [ '-DWANT_LINKER_MM' ]
endif

if get_option('arm_tracing')
  common_defines += [ '-DWANT_ARM_TRACING' ]
endif

if get_option('android_7_0_0')
  common_defines += [ '-DWANT_LINKER_N' ]
endif

if get_option('android_8_0_0')
  common_defines += [ '-DWANT_LINKER_O' ]
endif

if get_option('android_10_0_0')
  common_defines += [ '-DWANT_LINKER_Q' ]
endif

# Also replicate -std=gnu++11 from libhybris_common_la_CXXFLAGS
common_cpp_args = [
  '-std=gnu++11',
]

# ─────────────────────────────────────────────────────────────
# 4. LINKER FLAGS
# ─────────────────────────────────────────────────────────────
# In Autotools, we had -ldl, -lrt, -pthread, -lc, -lstdc++, etc.
# For simplicity, we add them via link_args. Alternatively, you can
# use find_library() for each library.

common_link_args = [
  '-ldl',
  '-lrt',
  '-pthread',
  '-lc',
  '-lstdc++',
  '-lpthread'
]

# ─────────────────────────────────────────────────────────────
# 5. LIBTOOL VERSIONING
# ─────────────────────────────────────────────────────────────
# The original Makefile used:
#   -version-info "$(LT_CURRENT):$(LT_REVISION):$(LT_AGE)"
# If you defined these as meson options, we can do this:

lt_current  = get_option('lt_current')   # e.g. 1
lt_revision = get_option('lt_revision')  # e.g. 0
lt_age      = get_option('lt_age')       # e.g. 0

# Meson "version" might be "current.revision.age", e.g. "1.0.0"
lib_version = '@0@.@1@.@2@'.format(lt_current, lt_revision, lt_age)
# Typically the "soversion" is current - age in libtool terms:
so_version = lt_current - lt_age

# ─────────────────────────────────────────────────────────────
# 6. BUILD THE SHARED LIBRARY
# ─────────────────────────────────────────────────────────────
# In Autotools, "libhybris-common.la" => "libhybris-common.so" here.

libhybris_common = shared_library(
  'hybris-common',  # library name (final .so name)
  sources:           common_c_sources + common_cpp_sources,
  include_directories: common_includes,
  c_args:            common_defines,   # includes preprocessor args for .c
  cpp_args:          common_cpp_args + common_defines,  # for .cpp
  link_args:         common_link_args,
  version:           lib_version,      # "1.0.0"
  soversion:         so_version.to_string(),  # "1", etc.
  install:           true
)
