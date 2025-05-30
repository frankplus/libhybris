# libhybris BUILD.gn Modular Reorganization

This document describes the ongoing conversion of the libhybris build system from Autotools (`Makefile.am`) to OpenHarmony's BUILD.gn build system, with a modular structure organized by components.

## Overview

The libhybris project is being converted from a traditional Autotools build system to OpenHarmony's BUILD.gn build system. This conversion creates a modular structure with separate `BUILD.gn` files for each component, following the same organization as the original `Makefile.am` autotools build system.

## Conversion Status

### Completed Components ✅
The following components have been successfully converted from `Makefile.am` to `BUILD.gn`:

- **Root BUILD.gn** - Global configuration and main group target
- **hybris/common/BUILD.gn** - Core libhybris-common + Android linker modules (jb, mm, n, o, q)
- **hybris/egl/BUILD.gn** - libEGL with GLVND support and platform abstraction
- **hybris/properties/BUILD.gn** - libandroid-properties
- **hybris/hardware/BUILD.gn** - libhardware
- **hybris/glesv1/BUILD.gn** - libGLESv1_CM
- **hybris/glesv2/BUILD.gn** - libGLESv2
- **hybris/camera/BUILD.gn** - libcamera
- **hybris/vibrator/BUILD.gn** - libvibrator
- **hybris/wifi/BUILD.gn** - libwifi
- **hybris/gralloc/BUILD.gn** - libgralloc
- **hybris/ui/BUILD.gn** - libui
- **hybris/input/BUILD.gn** - libis (input service)
- **hybris/sf/BUILD.gn** - libsf (Surface Flinger)
- **hybris/hwc2/BUILD.gn** - libhwc2 (Hardware Composer 2)
- **hybris/libnfc_nxp/BUILD.gn** - libnfc_nxp
- **hybris/libnfc_ndef_nxp/BUILD.gn** - libnfc_ndef_nxp
- **hybris/opencl/BUILD.gn** - libOpenCL
- **hybris/libsync/BUILD.gn** - libsync (conditional for Android 4.2+)
- **hybris/utils/BUILD.gn** - getprop and setprop utilities
- **hybris/tests/BUILD.gn** - Test executables

### Components Under Development 🚧
- **hybris/vulkan/BUILD.gn** - libvulkan (requires Vulkan headers)
- **hybris/media/BUILD.gn** - libmedia (complex multimedia components)
- **hybris/egl/platforms/BUILD.gn** - Platform-specific EGL implementations

### Currently Enabled in Main BUILD.gn
Only the following components are currently uncommented and actively built:
```gn
deps = [
  "hybris/common:libhybris-common",
  "hybris/egl:libEGL",
]

# Conditionally enabled for Android 10+
if (android_version_major >= 10) {
  deps += [ "hybris/common:q" ]
}
```

All other components are commented out pending testing and validation.

## Build System Structure

### Root BUILD.gn
- Contains global configuration via `libhybris_config`
- Defines build arguments imported from `libhybris_args.gni`
- Main group target that conditionally depends on subcomponents
- Includes Android version-specific defines and linker modules

### Component BUILD.gn Files

Each component has been converted to its own `BUILD.gn` file following the Autotools structure:

```
hybris/
├── common/BUILD.gn          # ✅ Core libhybris-common + Android linker modules (jb, mm, n, o, q)
├── properties/BUILD.gn      # ✅ libandroid-properties  
├── hardware/BUILD.gn        # ✅ libhardware
├── egl/BUILD.gn            # ✅ libEGL with GLVND and platforms support
│   └── platforms/BUILD.gn   # 🚧 Platform-specific implementations
├── glesv1/BUILD.gn         # ✅ libGLESv1_CM
├── glesv2/BUILD.gn         # ✅ libGLESv2
├── camera/BUILD.gn         # ✅ libcamera
├── vibrator/BUILD.gn       # ✅ libvibrator
├── media/BUILD.gn          # 🚧 libmedia (complex multimedia)
├── wifi/BUILD.gn           # ✅ libwifi
├── gralloc/BUILD.gn        # ✅ libgralloc
├── ui/BUILD.gn             # ✅ libui
├── input/BUILD.gn          # ✅ libis (input service)
├── sf/BUILD.gn             # ✅ libsf (Surface Flinger)
├── hwc2/BUILD.gn           # ✅ libhwc2 (Hardware Composer 2)
├── libnfc_nxp/BUILD.gn     # ✅ libnfc_nxp
├── libnfc_ndef_nxp/BUILD.gn # ✅ libnfc_ndef_nxp
├── opencl/BUILD.gn         # ✅ libOpenCL
├── vulkan/BUILD.gn         # 🚧 libvulkan (conditional on Vulkan headers)
├── libsync/BUILD.gn        # ✅ libsync (conditional for Android 4.2+)
├── utils/BUILD.gn          # ✅ getprop and setprop utilities
└── tests/BUILD.gn          # ✅ Test executables

Legend: ✅ Converted  🚧 In Progress  ❌ Not Started
```

### Mapping from Autotools Structure

The BUILD.gn structure maintains compatibility with the original `hybris/Makefile.am`:

```makefile
# Original Autotools SUBDIRS order:
SUBDIRS = include common properties hardware ui gralloc
if HAS_ANDROID_4_2_0
SUBDIRS += libsync
endif
SUBDIRS += platforms egl glesv1 glesv2 sf input camera vibrator media opencl wifi hwc2
if HAS_VULKAN_HEADERS  
SUBDIRS += vulkan
endif
SUBDIRS += libnfc_nxp libnfc_ndef_nxp
SUBDIRS += utils tests
```

This order is preserved in the BUILD.gn dependency structure.

## Android Version-Specific Linker Modules

The Android version-specific linker modules have been converted in `hybris/common/BUILD.gn`, matching the original `hybris/common/Makefile.am` structure:

- **jb** (JellyBean): Android API 16-20 (arm/x86 only, excluded from arm64/x86-64)
- **mm** (Marshmallow): Android API 21-23 (conditional: `HAS_ANDROID_6_0_0`)
- **n** (Nougat): Android API 24-25 (conditional: `HAS_ANDROID_7_0_0`)
- **o** (Oreo): Android API 26-28 (conditional: `HAS_ANDROID_8_0_0`)
- **q** (Q/Android 10): Android API 29+ (conditional: `HAS_ANDROID_10_0_0`)

Each linker module includes version-specific bionic headers and is conditionally built based on `android_version_major` setting, following the same logic as the original Autotools build.

### Current Implementation Status
- ✅ **q** linker module: Enabled for Android 10+ (`android_version_major >= 10`)
- ❌ **jb, mm, n, o** modules: Commented out pending validation
- 🚧 Architecture-specific builds: Logic implemented but commented out

## Key Features Implemented

### OpenHarmony Integration
- **ohos.gni imports**: All BUILD.gn files use `import("//build/ohos.gni")`
- **ohos_shared_library**: Standard OpenHarmony shared library targets
- **ohos_executable**: For utility programs (getprop, setprop, tests)
- **install_images**: Proper installation to system/updater partitions
- **part_name/subsystem_name**: Correct OpenHarmony component classification

### Build Configuration
- **libhybris_args.gni**: Centralized build arguments and feature flags
- **libhybris_config**: Global configuration shared across all components
- **Conditional compilation**: Android version and architecture-specific builds
- **Feature flags**: Debug, trace, quirks, and experimental features

### Advanced Features  
- **GLVND support**: EGL dispatch stub generation for multi-vendor graphics
- **Platform abstraction**: EGL platform-specific implementations
- **Test framework**: Comprehensive test suite conversion

## Benefits of BUILD.gn Conversion

1. **OpenHarmony Native Integration**: Full compatibility with OpenHarmony build system
2. **Modular Structure**: Each component can be built independently, matching autotools organization
3. **Maintainability**: Easier to maintain and understand individual components
4. **Scalability**: Easy to add new components or modify existing ones without affecting others
5. **Version Control**: Changes to one component don't impact other components
6. **Autotools Compatibility**: Preserves the original Makefile.am structure and build logic
7. **Conditional Building**: Proper handling of Android version and architecture dependencies
8. **Performance**: Faster builds through better dependency tracking and parallel execution

## Current Configuration

The build behavior is controlled by arguments in `libhybris_args.gni`:

```gn
# Android version compatibility
android_version_major = 11  # Determines which linker modules to build
android_version_minor = 0
android_version_patch = 0

# Architecture selection
hybris_arch = "arm64"       # Affects linker selection and defines

# Feature flags
hybris_enable_debug = false
hybris_enable_trace = false
hybris_enable_experimental = false
hybris_enable_adreno_quirks = false
hybris_enable_mali_quirks = false
hybris_enable_arm_tracing = false
hybris_enable_mesa = false
hybris_enable_wayland = false
hybris_enable_glvnd = false
hybris_enable_property_cache = false
hybris_enable_ubuntu_linker_overrides = false
hybris_enable_stub_linker = false

# Platform configuration
hybris_default_egl_platform = "null"
```

## Next Steps for Complete Conversion

### Phase 1: Enable Tested Components
1. Uncomment and test the following stable components in root BUILD.gn:
   ```gn
   "hybris/properties:libandroid-properties",
   "hybris/hardware:libhardware", 
   "hybris/glesv1:libGLESv1_CM",
   "hybris/glesv2:libGLESv2",
   "hybris/utils:getprop",
   "hybris/utils:setprop",
   ```

2. Validate and enable Android version-specific linker modules:
   ```gn
   # Add back conditionally
   if (hybris_arch != "arm64" && hybris_arch != "x86-64") {
     deps += [ "hybris/common:jb" ]
   }
   if (android_version_major >= 6) {
     deps += [ "hybris/common:mm" ]
   }
   ```

### Phase 2: Complete Complex Components
1. **Vulkan support**: Complete conditional Vulkan headers integration
2. **Media framework**: Validate complex multimedia component dependencies  
3. **Platform implementations**: Complete EGL platform-specific builds
4. **Test validation**: Enable and validate all test executables

### Phase 3: Integration Testing
1. **Cross-component testing**: Verify all inter-component dependencies
2. **Android version matrix**: Test across different Android API levels
3. **Architecture validation**: Test on arm, arm64, x86, x86-64 targets
4. **Feature flag testing**: Validate all build configuration options

## Build Validation Status

### Currently Buildable
The following minimal configuration is currently enabled and should build successfully:
```gn
# In root BUILD.gn group("libhybris"):
deps = [
  "hybris/common:libhybris-common",   # Core functionality 
  "hybris/egl:libEGL",                # EGL implementation
]

# Android 10+ linker module
if (android_version_major >= 10) {
  deps += [ "hybris/common:q" ]       # Q/Android 10 linker
}
```

### Build Command
To build the current libhybris components:
```bash
# From OpenHarmony build root
./build.sh --product-name {product} --target-name libhybris
```

### Known Issues
1. **Commented Dependencies**: Most components are commented out pending integration testing
2. **Cross-Component Validation**: Inter-component dependencies need validation
3. **Platform Testing**: Needs testing across different target architectures
4. **Android Version Matrix**: Requires validation across different Android API levels

### Testing Framework
The test suite has been converted but not yet enabled:
- `hybris/tests/BUILD.gn` contains test executables
- Tests include EGL configuration validation
- Hardware abstraction layer tests
- Platform-specific functionality tests

---

*Last Updated: May 30, 2025*  
*Conversion Status: Phase 1 - Core components converted, selective enabling in progress*
