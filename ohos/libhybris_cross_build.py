#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (c) 2025 OpenHarmony libhybris cross-compilation script

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import sys
import os
import argparse
import subprocess
import shutil
from pathlib import Path

# Copyright (c) 2025 OpenHarmony libhybris cross-compilation script

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import sys
import os
import argparse
import subprocess
import shutil
from pathlib import Path

sysroot_stub = 'sysroot_stub'
project_stub = 'project_stub'
android_headers_stub = 'android_headers_stub'

cross_file_content = '''
[properties]
needs_exe_wrapper = true

[built-in options]
c_args = [
    '--target=aarch64-linux-ohosmusl',
    '--sysroot=sysroot_stub',
    '-fno-emulated-tls',
    '-fPIC']

cpp_args = [
    '--target=aarch64-linux-ohosmusl',
    '--sysroot=sysroot_stub',
    '-fno-emulated-tls',
    '-fPIC']
    
c_link_args = [
    '--target=aarch64-linux-ohosmusl',
    '-fPIC',
    '--sysroot=sysroot_stub',
    '-Lsysroot_stub/usr/lib/aarch64-linux-ohos',
    '-Lproject_stub/prebuilts/clang/ohos/linux-x86_64/llvm/lib/clang/current/lib/aarch64-linux-ohos',
    '-Lproject_stub/prebuilts/clang/ohos/linux-x86_64/llvm/lib/aarch64-linux-ohos/c++',
    '--rtlib=compiler-rt']

cpp_link_args = [
    '--target=aarch64-linux-ohosmusl',
    '--sysroot=sysroot_stub',
    '-Lsysroot_stub/usr/lib/aarch64-linux-ohos',
    '-Lproject_stub/prebuilts/clang/ohos/linux-x86_64/llvm/lib/clang/current/lib/aarch64-linux-ohos',
    '-Lproject_stub/prebuilts/clang/ohos/linux-x86_64/llvm/lib/aarch64-linux-ohos/c++',
    '-fPIC',
    '-Wl,--exclude-libs=libunwind_llvm.a',
    '-Wl,--exclude-libs=libc++_static.a',
    '-Wl,--warn-shared-textrel',
    '--rtlib=compiler-rt']
	
[binaries]
ar = 'project_stub/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-ar'
c = ['ccache', 'project_stub/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang']
cpp = ['ccache', 'project_stub/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang++']
c_ld = 'lld'
cpp_ld = 'lld'
strip = 'project_stub/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-strip'
pkg-config = '/usr/bin/pkg-config'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
'''



def check_prerequisites():
    """Check if required tools are available"""
    required_tools = ['meson', 'ninja', 'ccache']
    missing_tools = []
    
    for tool in required_tools:
        if not shutil.which(tool):
            missing_tools.append(tool)
    
    if missing_tools:
        print(f"Error: Missing required tools: {', '.join(missing_tools)}")
        print("Please install them before running this script")
        return False
    
    return True

def generate_cross_file(project_path, sysroot_path, android_headers_path):
    """Generate meson cross-compilation file for OpenHarmony"""
    cross_file_path = "cross_file_ohos"
    
    with open(cross_file_path, 'w') as file:
        content = cross_file_content.replace("project_stub", project_path)
        content = content.replace("sysroot_stub", sysroot_path)
        content = content.replace("android_headers_stub", android_headers_path)
        file.write(content)
    
    print(f"Generated cross-compilation file: {cross_file_path}")
    return cross_file_path

def generate_pc_file(template, filename, prefix, version):
    """Generate a pkg-config file from template"""
    if not os.path.exists('pkgconfig'):
        os.makedirs('pkgconfig')
    
    pc_file_path = f'pkgconfig/{filename}'
    with open(pc_file_path, "w") as pc_file:
        content = template.replace("@PREFIX@", prefix)
        content = content.replace("@VERSION@", version)
        pc_file.write(content)
    
    print(f"Generated pkg-config file: {pc_file_path}")

def generate_pc_file_from_template(template_file, output_dir, substitutions):
    """Generate pkg-config file from template with variable substitutions"""
    print(f"Processing template: {template_file}")
    
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # Get output filename by removing .in extension
    template_name = os.path.basename(template_file)
    if template_name.endswith('.pc.in'):
        output_name = template_name[:-3]  # Remove .in
    else:
        output_name = template_name
    
    output_file = os.path.join(output_dir, output_name)
    
    with open(template_file, 'r') as template:
        content = template.read()
        
        # Apply substitutions
        for key, value in substitutions.items():
            content = content.replace(f'@{key}@', value)
        
        with open(output_file, 'w') as pc_file:
            pc_file.write(content)
    
    print(f"Generated pkg-config file: {output_file}")

def process_existing_pkgconfig_templates(hybris_source_dir, project_dir, product_name, install_prefix, version):
    """Process existing pkg-config templates from hybris source"""
    output_dir = 'pkgconfig'
    
    # Define substitutions for OpenHarmony
    substitutions = {
        'prefix': install_prefix,
        'libdir': f'{install_prefix}/lib',
        'includedir': f'{install_prefix}/include',
        'VERSION': version,
        'ANDROID_HEADERS_PKGCONFIG': '',  # Empty for OpenHarmony
        'ANDROID_HEADERS_CFLAGS': '',     # Empty for OpenHarmony
    }
    
    # Find all .pc.in files in the hybris source
    pkgconfig_templates = []
    for root, dirs, files in os.walk(hybris_source_dir):
        for file in files:
            if file.endswith('.pc.in'):
                pkgconfig_templates.append(os.path.join(root, file))
    
    if not pkgconfig_templates:
        print(f"Warning: No pkg-config templates found in {hybris_source_dir}")
        return
    
    print(f"Found {len(pkgconfig_templates)} pkg-config templates")
    
    # Process each template
    for template_path in pkgconfig_templates:
        try:
            generate_pc_file_from_template(template_path, output_dir, substitutions)
        except Exception as e:
            print(f"Warning: Failed to process template {template_path}: {e}")
    
    print("Processed existing pkg-config templates")

def prepare_build_environment(project_path, product, android_headers_path, install_prefix, source_dir):
    """Prepare the build environment for OpenHarmony cross-compilation"""
    global project_stub, sysroot_stub
    
    product = product.lower()
    project_stub = project_path
    sysroot_stub = os.path.join(project_stub, "out", product, "obj", "third_party", "musl")
    
    # Verify paths exist
    if not os.path.exists(project_path):
        print(f"Error: OpenHarmony project path does not exist: {project_path}")
        return None
    
    if not os.path.exists(sysroot_stub):
        print(f"Error: Sysroot path does not exist: {sysroot_stub}")
        print("Make sure you have built OpenHarmony for the specified product")
        return None
    
    if android_headers_path and not os.path.exists(android_headers_path):
        print(f"Error: Android headers path does not exist: {android_headers_path}")
        return None
    
    cross_file = generate_cross_file(project_path, sysroot_stub, android_headers_path or "")
    
    # Process existing pkg-config templates from hybris source
    process_existing_pkgconfig_templates(source_dir, project_path, product, install_prefix, "0.1.0")
    
    return cross_file

def get_meson_options(args):
    """Build meson setup options based on arguments"""
    options = []
    
    # Architecture
    if args.arch:
        options.append(f"-Darch={args.arch}")
    else:
        options.append("-Darch=arm64")  # Default for OpenHarmony
    
    # Android headers
    if args.android_headers:
        options.append(f"-Dandroid_headers_dir={args.android_headers}")
    
    # Debug mode
    if args.debug:
        options.append("-Denable_debug=true")
        options.append("--buildtype=debug")
    else:
        options.append("--buildtype=release")
    
    # Disable features that might not be available on OpenHarmony
    options.extend([
        "-Dwayland=false",
        "-Dubuntu_camera_headers=false",
        "-Dubuntu_linker_overrides=false",
        "-Dglvnd=false",
        "-Dproperty_cache=true"
    ])
    
    # Enable tracing if requested
    if args.enable_trace:
        options.append("-Denable_trace=true")
    
    return options

def run_meson_setup(cross_file, build_dir, source_dir, options, install_prefix):
    """Run meson setup with cross-compilation"""
    cmd = [
        "meson", "setup", build_dir, source_dir,
        f"--cross-file={cross_file}",
        f"--prefix={install_prefix}"
    ] + options
    
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"Meson setup failed:")
        print(f"stdout: {result.stdout}")
        print(f"stderr: {result.stderr}")
        return False
    
    print("Meson setup completed successfully")
    return True

def run_meson_compile(build_dir, jobs=None):
    """Run meson compile"""
    cmd = ["meson", "compile", "-C", build_dir]
    if jobs:
        cmd.extend(["-j", str(jobs)])
    
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd)
    
    if result.returncode != 0:
        print("Compilation failed")
        return False
    
    print("Compilation completed successfully")
    return True

def run_meson_install(build_dir):
    """Run meson install"""
    cmd = ["meson", "install", "-C", build_dir]
    
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd)
    
    if result.returncode != 0:
        print("Installation failed")
        return False
    
    print("Installation completed successfully")
    return True

def main():
    parser = argparse.ArgumentParser(
        description="Cross-compile libhybris for OpenHarmony platform",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s /path/to/openharmony rk3568 --android-headers /path/to/android/headers
  %(prog)s /path/to/openharmony hispark_taurus_standard --arch arm64 --debug
  %(prog)s /path/to/openharmony rk3568 --build-only --jobs 8
        """
    )
    
    parser.add_argument('openharmony_dir', 
                       help='Path to OpenHarmony source directory')
    parser.add_argument('product', 
                       help='OpenHarmony product name (e.g., rk3568, hispark_taurus_standard)')
    parser.add_argument('--android-headers', 
                       help='Path to Android headers directory')
    parser.add_argument('--arch', choices=['arm', 'arm64', 'x86', 'x86-64'],
                       help='Target architecture (default: arm64)')
    parser.add_argument('--build-dir', default='build_ohos',
                       help='Build directory (default: build_ohos)')
    parser.add_argument('--install-prefix', default='/usr/local',
                       help='Installation prefix (default: /usr/local)')
    parser.add_argument('--source-dir', default='../hybris',
                       help='Source directory relative to current (default: ../hybris)')
    parser.add_argument('--jobs', type=int,
                       help='Number of parallel build jobs')
    parser.add_argument('--debug', action='store_true',
                       help='Enable debug build')
    parser.add_argument('--enable-trace', action='store_true',
                       help='Enable tracing support')
    parser.add_argument('--setup-only', action='store_true',
                       help='Only run meson setup, do not compile')
    parser.add_argument('--build-only', action='store_true',
                       help='Only compile, assume setup is done')
    parser.add_argument('--install', action='store_true',
                       help='Install after successful compilation')
    
    args = parser.parse_args()
    
    # Check prerequisites
    if not check_prerequisites():
        return 1
    
    # Get absolute paths
    openharmony_dir = os.path.abspath(args.openharmony_dir)
    source_dir = os.path.abspath(args.source_dir)
    build_dir = os.path.abspath(args.build_dir)
    
    # Validate source directory
    if not os.path.exists(source_dir):
        print(f"Error: Source directory does not exist: {source_dir}")
        return 1
    
    if not os.path.exists(os.path.join(source_dir, "meson.build")):
        print(f"Error: meson.build not found in source directory: {source_dir}")
        return 1
    
    # Setup environment unless build-only mode
    if not args.build_only:
        cross_file = prepare_build_environment(
            openharmony_dir, 
            args.product, 
            args.android_headers,
            args.install_prefix,
            source_dir
        )
        
        if not cross_file:
            return 1
        
        # Get meson options
        meson_options = get_meson_options(args)
        
        # Run meson setup
        if not run_meson_setup(cross_file, build_dir, source_dir, meson_options, args.install_prefix):
            return 1
        
        if args.setup_only:
            print("Setup completed. Run with --build-only to compile.")
            return 0
    
    # Compile
    if not run_meson_compile(build_dir, args.jobs):
        return 1
    
    # Install if requested
    if args.install:
        if not run_meson_install(build_dir):
            return 1
    
    print("\n=== Build Summary ===")
    print(f"Source directory: {source_dir}")
    print(f"Build directory: {build_dir}")
    print(f"OpenHarmony project: {openharmony_dir}")
    print(f"Product: {args.product}")
    print(f"Architecture: {args.arch or 'arm64'}")
    if args.install:
        print(f"Install prefix: {args.install_prefix}")
    print("\nBuild completed successfully!")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
