/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <android-config.h>
#include <ws.h>
#include "ohos_window.h"
#include <malloc.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <mutex>
#include <vector>
#include <algorithm>
#include <dlfcn.h>

#ifndef RTLD_LAZY
#define RTLD_LAZY 1
#endif
#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0x00100
#endif

#include <hilog/log.h>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0xD001400
#define LOG_TAG "HybrisOhosEGL"

extern "C" {
#include <eglplatformcommon.h>
};

#include "logging.h"
#define HYBRIS_EGL_TRACE(message, ...) HYBRIS_DEBUG_LOG(EGL, message, ##__VA_ARGS__)
#include <hybris/gralloc/gralloc.h>

/* android_dlopen from libhybris-common — forward declaration matches binding.h */
extern "C" void* android_dlopen(const char* filename, int flags);

// OpenHarmony native window management
static std::vector<OhosNativeWindow *> _nativewindows;
static std::mutex _nativewindows_mutex;

extern "C" void ohosws_init_module(struct ws_egl_interface *egl_iface)
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_init_module enter");
    TRACE("ohosws_init_module(egl_iface=%p)", egl_iface);
    
    /*
     * Pre-load gralloc mapper HAL so hybris_gralloc_import_buffer works in this process.
     * GraphicBufferMapper::getInstance() loads the gralloc mapper via
     * android_load_sphal_library when the first GPU buffer operation is requested.
     * Pre-loading here ensures it is already in the hybris linker's table so
     * the SPHAL namespace bypass hook (_hybris_hook_android_load_sphal_library)
     * can intercept the call successfully.
     */
    static const char* kMapperPaths[] = {
        "/android/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl-mediatek.so",
        "/android/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl.so",
        nullptr,
    };

    for (int i = 0; kMapperPaths[i]; i++) {
        void* h = android_dlopen(kMapperPaths[i], RTLD_LAZY | RTLD_GLOBAL);
        if (h) {
            HYBRIS_INFO("Pre-loaded gralloc mapper: %s", kMapperPaths[i]);
            break;
        }
    }

    // Initialize gralloc for buffer management
    hybris_gralloc_initialize(0);
    
    // Initialize common EGL platform functionality
    eglplatformcommon_init(egl_iface);
    
    HYBRIS_INFO("OpenHarmony EGL platform initialized");
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_init_module exit");
}

extern "C" _EGLDisplay *ohosws_GetDisplay(EGLNativeDisplayType display)
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_GetDisplay enter: display=%p", (void*)display);
    HYBRIS_EGL_TRACE("ohosws_GetDisplay(display=%p)", (void*)display);
    
    // For OpenHarmony, we create a display for any non-null display parameter
    // or for the default display
    _EGLDisplay *dpy = nullptr;
    
    if (display == EGL_DEFAULT_DISPLAY || display != nullptr) {
        dpy = new _EGLDisplay;
        HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_GetDisplay created dpy: %p", dpy);
        HYBRIS_EGL_TRACE("Created EGL display: %p", dpy);
    } else {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG, "ohosws_GetDisplay: Invalid display parameter");
        HYBRIS_ERROR("Invalid display parameter");
    }
    
    return dpy;
}

extern "C" void ohosws_releaseDisplay(_EGLDisplay *dpy)
{
    HYBRIS_EGL_TRACE("ohosws_releaseDisplay(dpy=%p)", dpy);
    
    if (dpy) {
        delete dpy;
    }
}

extern "C" EGLNativeWindowType ohosws_CreateWindow(EGLNativeWindowType win, _EGLDisplay *display)
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_CreateWindow enter: win=%p display=%p", (void*)win, display);
    HYBRIS_EGL_TRACE("ohosws_CreateWindow(win=%p, display=%p)", (void*)win, display);
    
    // If win is null, we can't create a window
    if (!win) {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG, "Cannot create window with null native window");
        HYBRIS_ERROR("Cannot create window with null native window");
        return 0;
    }
    
    // Cast the input to OpenHarmony NativeWindow
    NativeWindow *nativeWindow = reinterpret_cast<NativeWindow *>(win);
    
    // Create our wrapper window
    OhosNativeWindow *window = new OhosNativeWindow(nativeWindow);
    if (!window) {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG, "Failed to create OhosNativeWindow");
        HYBRIS_ERROR("Failed to create OhosNativeWindow");
        return 0;
    }
    
    // Add reference and track the window
    window->common.incRef(&window->common);
    
    {
        std::lock_guard<std::mutex> lock(_nativewindows_mutex);
        _nativewindows.push_back(window);
    }
    
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_CreateWindow exit: window=%p", window);
    HYBRIS_EGL_TRACE("Created OpenHarmony window: %p", window);
    return reinterpret_cast<EGLNativeWindowType>(static_cast<struct ANativeWindow *>(window));
}

extern "C" void ohosws_DestroyWindow(EGLNativeWindowType win)
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_DestroyWindow enter: win=%p", (void*)win);
    HYBRIS_EGL_TRACE("ohosws_DestroyWindow(win=%p)", (void*)win);
    
    if (!win) {
        HYBRIS_ERROR("Cannot destroy null window");
        return;
    }
    
    OhosNativeWindow *window = static_cast<OhosNativeWindow *>(reinterpret_cast<struct ANativeWindow *>(win));
    
    // Remove from tracking list
    {
        std::lock_guard<std::mutex> lock(_nativewindows_mutex);
        auto it = std::find(_nativewindows.begin(), _nativewindows.end(), window);
        if (it != _nativewindows.end()) {
            _nativewindows.erase(it);
        }
    }
    
    // Release reference - window will be deleted when reference count reaches 0
    window->common.decRef(&window->common);
    
    HYBRIS_EGL_TRACE("Destroyed OpenHarmony window: %p", window);
}

extern "C" __eglMustCastToProperFunctionPointerType ohosws_eglGetProcAddress(const char *procname) 
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_eglGetProcAddress: %s", procname ? procname : "NULL");
    HYBRIS_EGL_TRACE("ohosws_eglGetProcAddress(procname=%s)", procname ? procname : "NULL");
    
    return eglplatformcommon_eglGetProcAddress(procname);
}

extern "C" void ohosws_passthroughImageKHR(EGLContext *ctx, EGLenum *target, EGLClientBuffer *buffer, const EGLint **attrib_list)
{
    HYBRIS_EGL_TRACE("ohosws_passthroughImageKHR(ctx=%p, target=%p, buffer=%p, attrib_list=%p)", ctx, target, buffer, attrib_list);
    
    eglplatformcommon_passthroughImageKHR(ctx, target, buffer, attrib_list);
}

extern "C" const char *ohosws_eglQueryString(EGLDisplay dpy, EGLint name, const char *(*real_eglQueryString)(EGLDisplay dpy, EGLint name))
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_eglQueryString: dpy=%p name=%d", (void*)dpy, name);
    HYBRIS_EGL_TRACE("ohosws_eglQueryString(dpy=%p, name=%d)", dpy, name);
    
    const char *ret = eglplatformcommon_eglQueryString(dpy, name, real_eglQueryString);
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_eglQueryString result: %s", ret ? ret : "NULL");
    return ret;
}

extern "C" void ohosws_prepareSwap(EGLDisplay dpy, EGLNativeWindowType win, EGLint *damage_rects, EGLint damage_n_rects)
{
    HYBRIS_EGL_TRACE("ohosws_prepareSwap(dpy=%p, win=%p, damage_rects=%p, damage_n_rects=%d)", dpy, (void*)win, damage_rects, damage_n_rects);
    
    // OpenHarmony handles damage tracking internally, so we don't need to do anything special here
    // Just log for debugging purposes
    if (damage_rects && damage_n_rects > 0) {
        HYBRIS_EGL_TRACE("Swap preparation with %d damage rectangles", damage_n_rects);
    }
}

extern "C" void ohosws_finishSwap(EGLDisplay dpy, EGLNativeWindowType win)
{
    HYBRIS_EGL_TRACE("ohosws_finishSwap(dpy=%p, win=%p)", dpy, (void*)win);
    
    // For OpenHarmony, swap completion is handled by the native window system
    // No additional work needed here
}

extern "C" void ohosws_setSwapInterval(EGLDisplay dpy, EGLNativeWindowType win, EGLint interval)
{
    HYBRIS_EGL_TRACE("ohosws_setSwapInterval(dpy=%p, win=%p, interval=%d)", dpy, (void*)win, interval);
    
    if (!win) {
        HYBRIS_ERROR("Cannot set swap interval on null window");
        return;
    }
    
    OhosNativeWindow *window = static_cast<OhosNativeWindow *>((struct ANativeWindow *)win);
    
    // OpenHarmony doesn't directly support swap interval configuration in the same way
    // Log the request but don't perform any action
    HYBRIS_EGL_TRACE("Swap interval %d requested for OpenHarmony window (not directly supported)", interval);
}

extern "C" void ohosws_eglInitialized(_EGLDisplay *dpy)
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "ohosws_eglInitialized: dpy=%p", dpy);
    HYBRIS_EGL_TRACE("ohosws_eglInitialized(dpy=%p)", dpy);
}

// Module interface structure
struct ws_module ws_module_info = {
    ohosws_init_module,
    ohosws_GetDisplay,
    NULL, // Terminate - not needed for OpenHarmony
    ohosws_CreateWindow,
    ohosws_DestroyWindow,
    ohosws_eglGetProcAddress,
    ohosws_passthroughImageKHR,
    ohosws_eglQueryString,
    ohosws_prepareSwap,
    ohosws_finishSwap,
    ohosws_setSwapInterval,
    ohosws_releaseDisplay,
    ohosws_eglInitialized,
};

// vim: noai:ts=4:sw=4:ss=4:expandtab
