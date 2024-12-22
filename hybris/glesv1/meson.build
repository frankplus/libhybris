# You could also put a mini-project() at the top of this file, but
# usually we rely on the top-level meson.build's project() call.

# Read Meson options (equivalent to Autotools conditionals)
glvnd  = get_option('glvnd')
mesa   = get_option('mesa')
enable_debug  = get_option('enable_debug')
trace  = get_option('trace')

# This suffix matches the old $(GL_LIB_SUFFIX).
# If e.g. gl_lib_suffix = '_libhybris', then the final library will be named
# libGLESv1_CM_libhybris.so
gl_lib_suffix = get_option('gl_lib_suffix')
if gl_lib_suffix == ''
  libname = 'GLESv1_CM'
else
  libname = 'GLESv1_CM' + gl_lib_suffix
endif

# Gather sources (from Makefile.am: glesv1_cm.c)
glesv1_sources = [
  'glesv1_cm.c'
]

# Construct compile args
glesv1_cflags = []
# top_srcdir/include --> we assume that is ../include relative to glesv1
incdir = include_directories('..', '../include')

# Also incorporate anything the user puts in --android_headers_cflags
if get_option('android_headers_cflags') != ''
  glesv1_cflags += meson.get_compiler('c').get_supported_arguments(
    get_option('android_headers_cflags').split()
  )
endif

# Replicate the if WANT_MESA => -DLIBHYBRIS_WANTS_MESA_X11_HEADERS
if mesa
  glesv1_cflags += ['-DLIBHYBRIS_WANTS_MESA_X11_HEADERS']
endif

# if WANT_TRACE => -DDEBUG
if trace
  glesv1_cflags += ['-DDEBUG']
endif

# if WANT_DEBUG => -ggdb -O0
if enable_debug
  glesv1_cflags += ['-ggdb', '-O0']
endif

# In Autotools: -version-info "1:1:0" is a libtool version (current:revision:age).
# Typically, Meson’s `version:` is set to the full “dynamic” version (like "1.1.0").
# Additionally you can set `soversion:` to the “major” if you want the SONAME to be
# something stable, e.g. "1".  Adjust as needed.
glesv1_version = '1.1.0'
glesv1_soversion = '1'

glesv1_lib = shared_library(
  libname,
  glesv1_sources,
  include_directories : incdir,
  c_args             : glesv1_cflags,
  version            : glesv1_version,
  soversion          : glesv1_soversion,
  # If you had other libraries (like ../common/libhybris-common.la), you’d link them here:
  # dependencies       : [ hybris_common_dep ],
  # Or link_args       : [ '-Lpath-to-common', '-lhybris-common' ],
  install            : true
)

#
#   Installing .pc only if !WANT_GLVND
#
if not glvnd
  # The standard location for pkgconfig files:
  pkgconfig_dir = join_paths(get_option('libdir'), 'pkgconfig')

  # Create a .pc file from a template (glesv1_cm.pc.in) at build time
  configure_file(
    input  : 'glesv1_cm.pc.in',
    output : 'glesv1_cm.pc',
    configuration : {
      'prefix'      : get_option('prefix'),
      'libdir'      : get_option('libdir'),
      'includedir'  : get_option('includedir'),
      # If you want to suffix the library in your .pc, you can do so here
      'libname'     : libname,
      'VERSION'     : meson.project_version(),
    },
    install_dir : pkgconfig_dir
  )
endif
