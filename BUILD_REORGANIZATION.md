# libhybris BUILD.gn Modular Reorganization

This document describes the reorganization of the libhybris BUILD.gn build system to match the original Autotools structure with Android version-specific components in subdirectories.

## Overview

The original monolithic `BUILD.gn` file (1129 lines) has been split into a modular structure with separate `BUILD.gn` files for each component, following the same organization as the original `Makefile.am` autotools build system.

## New Structure

### Root BUILD.gn
- Contains global configuration and build arguments
- Defines the main `libhybris_config` with all common settings
- Main group target that depends on all subcomponents
- Conditionally includes Android version-specific linker modules

### Component BUILD.gn Files

Each component now has its own `BUILD.gn` file in its respective subdirectory:

```
hybris/
├── common/BUILD.gn          # Core libhybris-common + Android linker modules (jb, mm, n, o, q)
├── properties/BUILD.gn      # libandroid-properties
├── hardware/BUILD.gn        # libhardware
├── egl/BUILD.gn            # libEGL
├── glesv1/BUILD.gn         # libGLESv1_CM
├── glesv2/BUILD.gn         # libGLESv2
├── platforms/BUILD.gn       # Platform abstraction libraries
├── camera/BUILD.gn         # libcamera
├── vibrator/BUILD.gn       # libvibrator
├── media/BUILD.gn          # libmedia
├── wifi/BUILD.gn           # libwifi
├── gralloc/BUILD.gn        # libgralloc
├── ui/BUILD.gn             # libui
├── input/BUILD.gn          # libis (input service)
├── sf/BUILD.gn             # libsf (Surface Flinger)
├── hwc2/BUILD.gn           # libhwc2 (Hardware Composer 2)
├── libnfc_nxp/BUILD.gn     # libnfc_nxp
├── libnfc_ndef_nxp/BUILD.gn # libnfc_ndef_nxp
├── opencl/BUILD.gn         # libOpenCL
├── vulkan/BUILD.gn         # libvulkan
├── libsync/BUILD.gn        # libsync (conditional for Android 4.2+)
└── utils/BUILD.gn          # getprop and setprop utilities
```

## Android Version-Specific Linker Modules

The Android version-specific linker modules are now properly organized in `hybris/common/BUILD.gn`:

- **jb** (JellyBean): Android API 16-20 (arm/x86 only)
- **mm** (Marshmallow): Android API 21-23
- **n** (Nougat): Android API 24-25  
- **o** (Oreo): Android API 26-28
- **q** (Q/Android 10): Android API 29+

Each linker module includes version-specific bionic headers and is conditionally built based on `android_version_major` setting.

## Benefits

1. **Modular Structure**: Each component can be built independently
2. **Maintainability**: Easier to maintain and understand individual components
3. **Scalability**: Easy to add new components or modify existing ones
4. **Version Control**: Changes to one component don't affect others
5. **Autotools Compatibility**: Matches the original Makefile.am structure
6. **Conditional Building**: Proper handling of Android version dependencies

## Configuration

The build behavior is controlled by the same arguments as before:

```gn
# Android version
android_version_major = 11  # Determines which linker modules to build

# Architecture  
hybris_arch = "arm64"       # Affects linker selection and defines

# Feature flags
hybris_enable_debug = false
hybris_enable_trace = false
# ... other options remain the same
```

## Migration Notes

- All existing build arguments and configurations are preserved
- Each component's dependencies are properly maintained using relative paths
- The global configuration is referenced from subdirectories using "../../:libhybris_config"
