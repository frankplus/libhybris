# common/q/meson.build

# First, list the common sources from q_la_SOURCES
# (Meson can build a single target mixing .c, .cpp, and .S files.)
q_sources = [
  'hybris_compat.cpp',
  'bionic/libc/bionic/bionic_call_ifunc_resolver.cpp',
  'dlfcn.cpp',
  'linker_allocator.cpp',
  'linker_block_allocator.cpp',
  'linker.cpp',
  'linker_cfi.cpp',
  'linker_config.cpp',
  'linker_dlwarning.cpp',
  'linker_gdb_support.cpp',
  'linker_globals.cpp',
  'linker_logger.cpp',
  'linker_main.cpp',
  'linker_mapped_file_fragment.cpp',
  'linker_memory.cpp',
  'linker_namespaces.cpp',
  'linker_phdr.cpp',
  'linker_sdk_versions.cpp',
  'linker_soinfo.cpp',
  'linker_tls.cpp',
  'linker_utils.cpp',
  'rt.cpp',
  '../strlcpy.c',
  '../strlcat.c',
  'bionic/libc/bionic/bionic_elf_tls.cpp',
  'bionic/libc/bionic/bionic_allocator.cpp',
]

# Conditionally add tlsdesc_resolver.S if arch_arm64 was requested
if get_option('arch_arm64')
  q_sources += ['tlsdesc_resolver.S']
endif

#
# Compiler and preprocessor flags
#

# C compiler flags from AM_CFLAGS + AM_CPPFLAGS in Makefile.am
c_args = [
  '-std=gnu99',
  '-fno-stack-protector',
  '-Wstrict-overflow=5',
]

# C++ compiler flags from AM_CXXFLAGS + AM_CPPFLAGS in Makefile.am
cxx_args = [
  '-std=gnu++11',
  '-Wold-style-cast',
  '-fno-stack-protector',    # replicate from AM_CPPFLAGS
  '-Wstrict-overflow=5',     # replicate from AM_CPPFLAGS
]

# The “Android headers” cflags come from your Meson option('android_headers_cflags')
android_headers_cflags = get_option('android_headers_cflags')
if android_headers_cflags != ''
  c_args   += [ android_headers_cflags ]
  cxx_args += [ android_headers_cflags ]
endif

# Include paths and defines from q_la_CPPFLAGS
# (Note that Meson encourages using 'include_directories()' rather than raw -I flags,
#  but we can append raw -I for simplicity or if you have unusual include paths.)
inc_args = [
  '-I' + meson.source_root() + '/include',
  '-I' + meson.source_root() + '/common',
  '-I' + meson.source_root() + '/common/q/bionic/libc',
  '-I' + meson.source_root() + '/common/q/bionic/libc/include',
  '-DDEFAULT_HYBRIS_LD_LIBRARY_PATH="@DEFAULT_HYBRIS_LD_LIBRARY_PATH@"',
  '-D_USING_LIBCXX',
]

#
# Optional defines from conditional blocks in Makefile.am
#

# if HAS_ANDROID_11_0_0, then -DHAS_ANDROID_11_0_0
# (By default, meson_options.txt does not define android_11_0_0, but if you add one:)
# if get_option('android_11_0_0')
#   inc_args += ['-DHAS_ANDROID_11_0_0']
# endif

# if WANT_DEBUG => -DTRACE_DEBUG=1
# (Maps to meson option('enable_debug'))
if get_option('enable_debug')
  inc_args += ['-DTRACE_DEBUG=1']
else
  # The Makefile uses the same -DTRACE_DEBUG=1 even in the 'else' branch,
  # so you could skip the else entirely; or replicate the same define anyway:
  inc_args += ['-DTRACE_DEBUG=1']
endif

# if WANT_ARM_TRACING => -DWANT_ARM_TRACING
# (Maps to meson option('arm_tracing'))
if get_option('arm_tracing')
  inc_args += ['-DWANT_ARM_TRACING']
endif

#
# Link flags
#
# -module, -avoid-version in libtool typically produce a plugin (.so) without library versioning.
# Meson provides shared_module() to mirror that notion of a plugin.  If you do want a normal
# shared library with typical so-versioning, use shared_library().
#
# Because the Makefile uses:
#   q_la_LDFLAGS = -module -avoid-version
# we use shared_module() here to replicate the "module" semantics.
#
ld_args = [
  # If you had any special link args beyond -module -avoid-version, list them here
]

#
# Build the module and install it under "libhybris/linker"
# (In Autotools, modlibexecdir = $(libdir)/libhybris/linker.)
# By default, Meson will prepend the standard installation prefix to this path.
#
q_mod = shared_module(
  'q',
  sources    : q_sources,
  c_args     : c_args + inc_args,  # pass inc_args to c compiler as well
  cpp_args   : cxx_args + inc_args,
  link_args  : ld_args,
  install    : true,
  install_dir: 'libhybris/linker',
)

