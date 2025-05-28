# libhybris Cross-compilation for OpenHarmony

This directory contains tools for cross-compiling libhybris for the OpenHarmony platform.

## Prerequisites

Before using the cross-compilation script, ensure you have:

1. **OpenHarmony SDK**: A complete OpenHarmony source tree that has been built for your target product
2. **Build tools**:
   - `meson` (build system)
   - `ninja` (build backend)
   - `ccache` (optional, for faster builds)
3. **Android headers**: Android headers compatible with your target platform

## Quick Start

### Basic cross-compilation:
```bash
./libhybris_cross_build.py /path/to/openharmony rk3568 --android-headers /path/to/android/headers
```

### Debug build:
```bash
./libhybris_cross_build.py /path/to/openharmony rk3568 --debug --android-headers /path/to/android/headers
```

### Parallel build with installation:
```bash
./libhybris_cross_build.py /path/to/openharmony rk3568 --android-headers /path/to/android/headers --jobs 8 --install
```

## Command Line Options

- `openharmony_dir`: Path to your OpenHarmony source directory
- `product`: OpenHarmony product name (e.g., `rk3568`, `hispark_taurus_standard`)
- `--android-headers`: Path to Android headers directory
- `--arch`: Target architecture (`arm`, `arm64`, `x86`, `x86-64`). Default: `arm64`
- `--build-dir`: Build directory name. Default: `build_ohos`
- `--install-prefix`: Installation prefix. Default: `/usr/local`
- `--source-dir`: libhybris source directory. Default: `../hybris`
- `--jobs`: Number of parallel build jobs
- `--debug`: Enable debug build
- `--enable-trace`: Enable tracing support
- `--setup-only`: Only run meson setup, don't compile
- `--build-only`: Only compile (assumes setup is already done)
- `--install`: Install after successful compilation

## Examples

### Setup and compile for RK3568:
```bash
./libhybris_cross_build.py \
    /home/user/openharmony \
    rk3568 \
    --android-headers /home/user/android-headers \
    --arch arm64 \
    --jobs 4
```

### Debug build with tracing:
```bash
./libhybris_cross_build.py \
    /home/user/openharmony \
    hispark_taurus_standard \
    --android-headers /home/user/android-headers \
    --debug \
    --enable-trace
```

### Two-stage build (useful for development):
```bash
# Setup only
./libhybris_cross_build.py \
    /home/user/openharmony \
    rk3568 \
    --android-headers /home/user/android-headers \
    --setup-only

# Compile only (can be run multiple times)
./libhybris_cross_build.py \
    /home/user/openharmony \
    rk3568 \
    --build-only \
    --jobs 8
```

## Important Notes

1. **OpenHarmony Build**: Your OpenHarmony project must be built for the target product before running this script. The script looks for the sysroot at `$OPENHARMONY_DIR/out/$PRODUCT/obj/third_party/musl`.

2. **Android Headers**: libhybris requires Android headers. You can either:
   - Provide them via `--android-headers` parameter
   - Use the `android-headers` pkg-config package if available

3. **Architecture**: The script defaults to `arm64` which is appropriate for most OpenHarmony devices. Adjust with `--arch` if needed.

4. **Cross-compilation Files**: The script generates:
   - `cross_file_ohos`: Meson cross-compilation configuration
   - `pkgconfig/`: Directory with pkg-config files for libhybris

## Troubleshooting

### "Sysroot path does not exist"
- Ensure OpenHarmony is built for the specified product
- Check that the product name is correct
- Verify the OpenHarmony directory path

### "Android headers path does not exist"
- Provide a valid path to Android headers
- Or ensure `android-headers` pkg-config package is installed

### Compilation errors
- Check that all OpenHarmony toolchain components are built
- Verify Android headers are compatible with libhybris version
- Try a debug build for more verbose output

## Output

After successful compilation:
- Libraries are built in `$BUILD_DIR/`
- If `--install` is used, libraries are installed to `$INSTALL_PREFIX/lib/`
- Headers are installed to `$INSTALL_PREFIX/include/hybris/`
- Pkg-config files are available for integration with other projects

## Integration

The built libhybris libraries can be integrated into OpenHarmony system images or used as dynamic libraries in OpenHarmony applications that need to interface with Android HAL components.
