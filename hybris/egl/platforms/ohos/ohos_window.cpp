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
#include "ohos_window.h"
#include "logging.h"
#define HYBRIS_EGL_TRACE(message, ...) HYBRIS_DEBUG_LOG(EGL, message, ##__VA_ARGS__)

#include <errno.h>
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

#if ANDROID_VERSION_MAJOR>=4 && ANDROID_VERSION_MINOR>=2 || ANDROID_VERSION_MAJOR>=5
extern "C" {
#include <sync/sync.h>
};
#endif

#include <hybris/gralloc/gralloc.h>

/* ─── OHOS→Android format mapping ─────────────────────────────────────────
 * OHOS PixelFormat values (from the display VDI):
 *   PIXEL_FMT_RGB_565=3, PIXEL_FMT_RGBX_8888=11, PIXEL_FMT_RGBA_8888=12,
 *   PIXEL_FMT_RGB_888=13, PIXEL_FMT_BGRA_8888=20
 * Android HAL_PIXEL_FORMAT_* values:
 *   RGBA_8888=1, RGBX_8888=2, RGB_888=3, RGB_565=4, BGRA_8888=5
 */
static int OhosFormatToAndroid(uint32_t ohosFormat)
{
    switch (ohosFormat) {
        case 3:  return 4;    /* PIXEL_FMT_RGB_565  → HAL_PIXEL_FORMAT_RGB_565  */
        case 11: return 2;    /* PIXEL_FMT_RGBX_8888 → HAL_PIXEL_FORMAT_RGBX_8888 */
        case 12: return 1;    /* PIXEL_FMT_RGBA_8888 → HAL_PIXEL_FORMAT_RGBA_8888 */
        case 13: return 3;    /* PIXEL_FMT_RGB_888  → HAL_PIXEL_FORMAT_RGB_888  */
        case 20: return 5;    /* PIXEL_FMT_BGRA_8888 → HAL_PIXEL_FORMAT_BGRA_8888 */
        default: return 0x22; /* HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED */
    }
}

/* ─── Import native handle from BufferHandle into the current process ───────
 * BufferHandle fds are duped by the OHOS IPC layer before reaching this
 * process, so bh.fd / bh.reserve[0..reserveFds-1] are valid fds here.
 * We reconstruct a raw native_handle_t (excluding the last two int slots that
 * hybris_buffer_vdi_impl uses to store a process-local pointer) and call
 * hybris_gralloc_import_buffer so the Mali GPU driver can access the
 * DMA-buf in this process via ANativeWindowBuffer::handle.
 */
#include <hilog/log.h>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0xD001400
#define LOG_TAG "HybrisOhosWin"

static constexpr int kPtrSlots = 2; /* int slots reserved for process-local ptr in reserve[] */

/* ─── Global OHNativeWindowBuffer* → OhosNativeWindowBuffer* lookup ─────────
 * Used by eglplatformcommon_passthroughImageKHR to translate the raw OHOS
 * buffer pointer passed to eglCreateImageKHR(EGL_NATIVE_BUFFER_OHOS) into the
 * ANativeWindowBuffer* wrapper that Mali EGL expects for EGL_NATIVE_BUFFER_ANDROID.
 *
 * Entries are added in OhosNativeWindow::dequeueBuffer() when a new wrapper is
 * created, and removed in OhosNativeWindow::freeBuffers() before the wrapper's
 * map-ownership reference is released.
 */
static std::mutex g_bufferLookupMutex;
static std::map<void*, OhosNativeWindowBuffer*> g_bufferLookup;

extern "C" ANativeWindowBuffer* ohosws_find_anwb_for_ohbuffer(void* ohBuf)
{
    if (!ohBuf) {
        return nullptr;
    }

    /* Fast path: buffer was registered by OhosNativeWindow::dequeueBuffer (app process). */
    {
        std::lock_guard<std::mutex> lock(g_bufferLookupMutex);
        auto it = g_bufferLookup.find(ohBuf);
        if (it != g_bufferLookup.end()) {
            HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
                       "ohosws_find_anwb_for_ohbuffer: found cached wrapper %p for ohBuf %p",
                       it->second, ohBuf);
            return it->second;
        }
    }

    /*
     * Slow path: consumer-side buffer (e.g. render_service calling eglCreateImageKHR
     * on a SurfaceBuffer it received from a client).  Create a wrapper on demand.
     *
     * Refcount starts at 0 (BaseNativeWindowBuffer default).  Mali EGL will incRef
     * to 1 inside eglCreateImageKHR and decRef to 0 (→ delete) inside
     * eglDestroyImageKHR, so no manual cleanup is needed here.
     */
    OhosNativeWindowBuffer* wrapper = new OhosNativeWindowBuffer(
        static_cast<OHNativeWindowBuffer*>(ohBuf));
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
               "ohosws_find_anwb_for_ohbuffer: created on-demand wrapper %p for ohBuf %p",
               wrapper, ohBuf);
    return wrapper;
}

static buffer_handle_t ImportNativeHandleFromBH(const BufferHandle& bh)
{
    int numFds  = 1 + static_cast<int>(bh.reserveFds);
    int numInts = (static_cast<int>(bh.reserveInts) > kPtrSlots)
                  ? static_cast<int>(bh.reserveInts) - kPtrSlots
                  : 0;

    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
               "ImportNativeHandleFromBH: bh=%p fd=%d numFds=%d numInts=%d %dx%d",
               &bh, bh.fd, numFds, numInts, bh.width, bh.height);

    native_handle_t* rawNh = native_handle_create(numFds, numInts);
    if (!rawNh) {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG,
                   "ImportNativeHandleFromBH: native_handle_create(%d,%d) OOM", numFds, numInts);
        return nullptr;
    }

    rawNh->data[0] = bh.fd;
    for (int i = 0; i < static_cast<int>(bh.reserveFds); ++i) {
        rawNh->data[1 + i] = bh.reserve[i];
    }
    for (int i = 0; i < numInts; ++i) {
        rawNh->data[numFds + i] = bh.reserve[bh.reserveFds + i];
    }

    buffer_handle_t imported = nullptr;
    int ret = hybris_gralloc_import_buffer(rawNh, &imported);
    native_handle_delete(rawNh); /* import copies the handle; free the temp struct */
    if (ret != 0 || !imported) {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG,
                   "ImportNativeHandleFromBH: import failed ret=%d fd=%d %dx%d",
                   ret, bh.fd, bh.width, bh.height);
        return nullptr;
    }
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
               "ImportNativeHandleFromBH: success, imported=%p", (void*)imported);
    return imported;
}

/* ─── OhosNativeWindowBuffer ─────────────────────────────────────────────── */

OhosNativeWindowBuffer::OhosNativeWindowBuffer(OHNativeWindowBuffer* ohBuffer)
    : BaseNativeWindowBuffer()
    , m_ohBuffer(ohBuffer)
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "OhosNativeWindowBuffer constructor: ohBuffer=%p", ohBuffer);
    HYBRIS_EGL_TRACE("OhosNativeWindowBuffer(%p)", ohBuffer);

    NativeObjectReference(ohBuffer);

    BufferHandle* bh = GetBufferHandleFromNative(ohBuffer);
    if (!bh) {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG, "GetBufferHandleFromNative returned null for %p", ohBuffer);
        HYBRIS_ERROR("GetBufferHandleFromNative returned null for %p", ohBuffer);
        return;
    }

    buffer_handle_t nh = ImportNativeHandleFromBH(*bh);
    if (!nh) {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG, "ImportNativeHandleFromBH failed for ohBuffer %p", ohBuffer);
        HYBRIS_ERROR("ImportNativeHandleFromBH failed for ohBuffer %p", ohBuffer);
        return;
    }

    m_importedHandle             = nh;
    ANativeWindowBuffer::handle  = nh;
    ANativeWindowBuffer::width   = bh->width;
    ANativeWindowBuffer::height  = bh->height;
    ANativeWindowBuffer::stride  = bh->stride;
    ANativeWindowBuffer::format  = OhosFormatToAndroid(bh->format);
    ANativeWindowBuffer::usage   = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;

    HYBRIS_EGL_TRACE("OhosNativeWindowBuffer: %dx%d stride=%d fmt=%d handle=%p",
          bh->width, bh->height, bh->stride, bh->format, nh);
}

OhosNativeWindowBuffer::~OhosNativeWindowBuffer()
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
               "~OhosNativeWindowBuffer: this=%p magic=0x%08x ohBuffer=%p importedHandle=%p",
               this, magic, m_ohBuffer, (void*)m_importedHandle);

    if (magic == kMagicDead) {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG,
                   "~OhosNativeWindowBuffer: DOUBLE DESTROY DETECTED this=%p — aborting release", this);
        return;
    }
    magic = kMagicDead;

    HYBRIS_EGL_TRACE("~OhosNativeWindowBuffer(%p)", m_ohBuffer);
    if (m_importedHandle) {
        HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
                   "~OhosNativeWindowBuffer: calling hybris_gralloc_release handle=%p", (void*)m_importedHandle);
        hybris_gralloc_release(m_importedHandle, 0);
        m_importedHandle = nullptr;
    }
    if (m_ohBuffer) {
        NativeObjectUnreference(m_ohBuffer);
        m_ohBuffer = nullptr;
    }
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
               "~OhosNativeWindowBuffer: done this=%p", this);
}

/* ─── OhosNativeWindow ───────────────────────────────────────────────────── */

OhosNativeWindow::OhosNativeWindow(NativeWindow *nativeWindow)
    : BaseNativeWindow()
    , m_nativeWindow(nativeWindow)
    , m_usage(0)
    , m_bufferCount(3)
    , m_width(0)
    , m_height(0)
    , m_format(PIXEL_FMT_RGBA_8888)
    , m_transform(0)
    , m_scalingMode(NATIVE_WINDOW_SCALING_MODE_FREEZE)
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "OhosNativeWindow constructor: nativeWindow=%p", nativeWindow);
    HYBRIS_EGL_TRACE("OhosNativeWindow::OhosNativeWindow(nativeWindow=%p)", nativeWindow);

    if (nativeWindow) {
        NativeObjectReference(nativeWindow);

        int32_t width, height;
        if (NativeWindowHandleOpt(nativeWindow, GET_BUFFER_GEOMETRY, &width, &height) == 0) {
            m_width = width;
            m_height = height;
        }

        int32_t format = PIXEL_FMT_RGBA_8888;
        if (NativeWindowHandleOpt(nativeWindow, GET_FORMAT, &format) == 0) {
            m_format = format;
        }
        HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "OhosNativeWindow initial geometry: %dx%d format=%d", m_width, m_height, m_format);
    }

    initializeDefaults();
}

OhosNativeWindow::~OhosNativeWindow()
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
               "~OhosNativeWindow: this=%p mapSize=%zu", this, m_bufferMap.size());
    HYBRIS_EGL_TRACE("OhosNativeWindow::~OhosNativeWindow()");

    freeBuffers("dtor");

    if (m_nativeWindow) {
        NativeObjectUnreference(m_nativeWindow);
        m_nativeWindow = nullptr;
    }
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
               "~OhosNativeWindow: done this=%p", this);
}

void OhosNativeWindow::initializeDefaults()
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "initializeDefaults: %dx%d", m_width, m_height);
    m_crop = {0, 0, m_width, m_height};
    m_usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;
}

void OhosNativeWindow::updateGeometry(int w, int h)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_width == 0 || m_height == 0) {
        m_width  = w;
        m_height = h;
        m_crop   = {0, 0, w, h};
        HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
                   "updateGeometry: late geometry resolved to %dx%d", w, h);
    }
}

void OhosNativeWindow::lock()
{
    m_mutex.lock();
}

void OhosNativeWindow::unlock()
{
    m_mutex.unlock();
}

void OhosNativeWindow::frame()
{
}

void OhosNativeWindow::resize(unsigned int width, unsigned int height)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::resize(width=%u, height=%u)", width, height);

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_width != (int)width || m_height != (int)height) {
        m_width = width;
        m_height = height;
        m_crop = {0, 0, (int32_t)width, (int32_t)height};
        HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
                   "resize: %dx%d (caller resize)", m_width, m_height);
        freeBuffers("resize"); /* drop cached wrappers; new ones created on next dequeue */
    }
}

/*
 * dequeueBuffer — obtain a buffer from the OHOS surface and return a properly
 * initialised ANativeWindowBuffer* (via OhosNativeWindowBuffer wrapper) to Mali.
 *
 * We cache wrappers in m_bufferMap because OHOS recycles a fixed buffer pool;
 * the same OHNativeWindowBuffer* will appear again after queueBuffer.
 */
int OhosNativeWindow::dequeueBuffer(BaseNativeWindowBuffer** buffer, int* fenceFd)
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "dequeueBuffer enter: win=%p geometry=%dx%d", this, m_width, m_height);
    HYBRIS_EGL_TRACE("OhosNativeWindow::dequeueBuffer()");

    if (!m_nativeWindow) {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG, "Native window is null");
        HYBRIS_ERROR("Native window is null");
        return -EINVAL;
    }

    OHNativeWindowBuffer* ohBuffer = nullptr;
    int fence = -1;

    int result = NativeWindowRequestBuffer(m_nativeWindow, &ohBuffer, &fence);
    if (result != 0) {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG, "NativeWindowRequestBuffer failed: %d", result);
        HYBRIS_ERROR("NativeWindowRequestBuffer failed: %d", result);
        return result;
    }
    if (!ohBuffer) {
        HiLogPrint(LOG_CORE, LOG_ERROR, LOG_DOMAIN, LOG_TAG, "NativeWindowRequestBuffer returned null buffer");
        HYBRIS_ERROR("NativeWindowRequestBuffer returned null buffer");
        return -EINVAL;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_bufferMap.find(ohBuffer);
    OhosNativeWindowBuffer* wrapper;
    if (it == m_bufferMap.end()) {
        HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "New buffer encountered: %p", ohBuffer);
        wrapper = new OhosNativeWindowBuffer(ohBuffer);
        /* Some producers (ArkWeb) hand us a NativeWindow whose
         * GET_BUFFER_GEOMETRY property was never set (reads back the 1x1
         * default) while the buffer pool is allocated at the real surface
         * size.  Reporting the phantom 1x1 through width()/height() poisons
         * Mali's surface state (EGL_WIDTH/EGL_HEIGHT/buffer-age bookkeeping)
         * and breaks Chromium's partial redraw — stale frames flicker on
         * screen.  Only in that degenerate case, trust the actual
         * BufferHandle dimensions.  (Never second-guess a real geometry the
         * client set explicitly — render_service intentionally runs with
         * window geometry transposed relative to its pre-rotated buffers.) */
        if (m_width <= 1 && m_height <= 1 &&
            wrapper->ANativeWindowBuffer::width > 1 && wrapper->ANativeWindowBuffer::height > 1) {
            HiLogPrint(LOG_CORE, LOG_WARN, LOG_DOMAIN, LOG_TAG,
                       "dequeueBuffer: window geometry %dx%d != buffer %dx%d — adopting buffer size",
                       m_width, m_height,
                       wrapper->ANativeWindowBuffer::width, wrapper->ANativeWindowBuffer::height);
            m_width  = wrapper->ANativeWindowBuffer::width;
            m_height = wrapper->ANativeWindowBuffer::height;
            m_crop   = {0, 0, m_width, m_height};
        }
        /* Take map-ownership reference so Mali's permanent incRef can never drive
         * refcount to 0 while the wrapper is still in the map.
         * Matching decRef is in freeBuffers(). */
        wrapper->common.incRef(&wrapper->common);
        m_bufferMap[ohBuffer] = wrapper;
        /* Register in global lookup for eglCreateImageKHR buffer translation. */
        {
            std::lock_guard<std::mutex> lookup_lock(g_bufferLookupMutex);
            g_bufferLookup[ohBuffer] = wrapper;
        }
    } else {
        wrapper = it->second;
    }

    *buffer = wrapper;
    *fenceFd = fence;

    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "dequeueBuffer exit: wrapper=%p ohBuffer=%p fence=%d", wrapper, ohBuffer, fence);
    HYBRIS_EGL_TRACE("OhosNativeWindow::dequeueBuffer() = wrapper %p (ohBuffer %p) fence=%d",
          wrapper, ohBuffer, fence);
    return 0;
}

/*
 * queueBuffer — present a rendered buffer back to the OHOS compositor.
 * Cast buffer back to OhosNativeWindowBuffer* to recover the OHNativeWindowBuffer*,
 * then call NativeWindowFlushBuffer.  The wrapper stays in m_bufferMap for reuse.
 */
int OhosNativeWindow::queueBuffer(BaseNativeWindowBuffer* buffer, int fenceFd)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::queueBuffer(buffer=%p, fenceFd=%d)", buffer, fenceFd);

    if (!m_nativeWindow || !buffer) {
        HYBRIS_ERROR("Invalid parameters: nativeWindow=%p, buffer=%p", m_nativeWindow, buffer);
        return -EINVAL;
    }

    OhosNativeWindowBuffer* wrapper = static_cast<OhosNativeWindowBuffer*>(buffer);
    OHNativeWindowBuffer* ohBuffer = wrapper->ohBuffer();

    Region dirty;
    Region::Rect ohRect;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ohRect.x = m_crop.left;
        ohRect.y = m_crop.top;
        ohRect.w = static_cast<uint32_t>(m_crop.right - m_crop.left);
        ohRect.h = static_cast<uint32_t>(m_crop.bottom - m_crop.top);
    }
    dirty.rectNumber = 1;
    dirty.rects = &ohRect;

    int result = NativeWindowFlushBuffer(m_nativeWindow, ohBuffer, fenceFd, dirty);
    if (result != 0) {
        HYBRIS_ERROR("NativeWindowFlushBuffer failed: %d", result);
    }
    return result;
}

/*
 * cancelBuffer — return a buffer to OHOS without presenting it.
 */
int OhosNativeWindow::cancelBuffer(BaseNativeWindowBuffer* buffer, int fenceFd)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::cancelBuffer(buffer=%p, fenceFd=%d)", buffer, fenceFd);

    if (!m_nativeWindow || !buffer) {
        HYBRIS_ERROR("Invalid parameters: nativeWindow=%p, buffer=%p", m_nativeWindow, buffer);
        return -EINVAL;
    }

    OhosNativeWindowBuffer* wrapper = static_cast<OhosNativeWindowBuffer*>(buffer);
    OHNativeWindowBuffer* ohBuffer = wrapper->ohBuffer();

    int result = NativeWindowCancelBuffer(m_nativeWindow, ohBuffer);
    if (result != 0) {
        HYBRIS_ERROR("NativeWindowCancelBuffer failed: %d", result);
    }

    if (fenceFd >= 0) {
        close(fenceFd);
    }
    return result;
}

int OhosNativeWindow::query(int what, int* value) const
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "query what=%d (UNUSED by BaseNativeWindow)", what);
    HYBRIS_EGL_TRACE("OhosNativeWindow::query(what=%d)", what);

    if (!value) {
        return -EINVAL;
    }

    switch (what) {
        case NATIVE_WINDOW_WIDTH:
            *value = width();
            return 0;
        case NATIVE_WINDOW_HEIGHT:
            *value = height();
            return 0;
        case NATIVE_WINDOW_FORMAT:
            *value = OhosFormatToAndroid(m_format);
            HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "query FORMAT=%d", *value);
            return 0;
        case NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS:
            *value = 1;
            HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "query MIN_UNDEQUEUED_BUFFERS=%d", *value);
            return 0;
        case NATIVE_WINDOW_CONSUMER_USAGE_BITS:
            *value = 0;
            HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "query CONSUMER_USAGE_BITS=%d", *value);
            return 0;
        case NATIVE_WINDOW_TRANSFORM_HINT:
            *value = m_transform;
            HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "query TRANSFORM_HINT=%d", *value);
            return 0;
        case NATIVE_WINDOW_CONCRETE_TYPE:
            *value = NATIVE_WINDOW_TYPE_OHOS;
            HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "query CONCRETE_TYPE=%d", *value);
            return 0;
        case NATIVE_WINDOW_DEFAULT_WIDTH:
            *value = width();
            return 0;
        case NATIVE_WINDOW_DEFAULT_HEIGHT:
            *value = height();
            return 0;

        default:
            HYBRIS_ERROR("Unknown query parameter: %d", what);
            return -EINVAL;
    }
}

int OhosNativeWindow::perform(int operation, va_list args)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::perform(operation=%d)", operation);
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "perform op=%d (UNUSED by BaseNativeWindow)", operation);

    switch (operation) {
        case NATIVE_WINDOW_SET_USAGE:
        {
            uint64_t usage = va_arg(args, uint64_t);
            return setUsage(usage);
        }
        case NATIVE_WINDOW_SET_BUFFER_COUNT:
        {
            int bufferCount = va_arg(args, int);
            return setBufferCount(bufferCount);
        }
        case NATIVE_WINDOW_SET_BUFFERS_GEOMETRY:
        {
            int width = va_arg(args, int);
            int height = va_arg(args, int);
            int format = va_arg(args, int);
            return setBufferGeometry(width, height, format);
        }
        case NATIVE_WINDOW_SET_BUFFERS_FORMAT:
        {
            int format = va_arg(args, int);
            return setBufferGeometry(m_width, m_height, format);
        }
        case NATIVE_WINDOW_SET_SCALING_MODE:
        {
            int mode = va_arg(args, int);
            return setScalingMode(mode);
        }
        case NATIVE_WINDOW_SET_BUFFERS_TRANSFORM:
        {
            int transform = va_arg(args, int);
            return setTransform(transform);
        }
        case NATIVE_WINDOW_SET_CROP:
        {
            android_native_rect_t const* rect = va_arg(args, android_native_rect_t const*);
            return setCrop(rect);
        }
        default:
            HYBRIS_ERROR("Unknown perform operation: %d", operation);
            return -EINVAL;
    }
}

int OhosNativeWindow::setUsage(uint64_t usage)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::setUsage(usage=0x%" PRIx64 ")", usage);
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "setUsage: 0x%" PRIx64, usage);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_usage = usage;

    if (m_nativeWindow) {
        return NativeWindowHandleOpt(m_nativeWindow, SET_USAGE, usage);
    }
    return 0;
}

int OhosNativeWindow::setBufferCount(int bufferCount)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::setBufferCount(bufferCount=%d)", bufferCount);
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "setBufferCount: %d", bufferCount);

    if (bufferCount < 1) {
        HYBRIS_ERROR("Invalid buffer count: %d", bufferCount);
        return -EINVAL;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_bufferCount = bufferCount;
    return 0;
}

int OhosNativeWindow::setBufferGeometry(int width, int height, int format)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::setBufferGeometry(width=%d, height=%d, format=%d)", width, height, format);
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "setBufferGeometry: %dx%d format=%d", width, height, format);

    if (width <= 0 || height <= 0) {
        HYBRIS_ERROR("Invalid geometry: %dx%d", width, height);
        return -EINVAL;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_width = width;
    m_height = height;
    m_format = format;
    m_crop = {0, 0, width, height};
    return 0;
}

int OhosNativeWindow::setScalingMode(int mode)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::setScalingMode(mode=%d)", mode);
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "setScalingMode: %d", mode);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scalingMode = mode;
    return 0;
}

int OhosNativeWindow::setTransform(int transform)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::setTransform(transform=%d)", transform);
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "setTransform: %d", transform);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_transform = transform;
    return 0;
}

int OhosNativeWindow::setCrop(android_native_rect_t const* rect)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::setCrop(rect=%p)", rect);
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "setCrop: %p", rect);

    if (!rect) {
        HYBRIS_ERROR("Null crop rectangle");
        return -EINVAL;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_crop = *rect;
    return 0;
}

void OhosNativeWindow::freeBuffers(const char* caller)
{
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
               "freeBuffers[%s]: win=%p mapSize=%zu", caller ? caller : "?", this, m_bufferMap.size());
    /* Release map-ownership references. Each wrapper was incRef'd when inserted
     * into the map (see dequeueBuffer). Using decRef (not raw delete) ensures we
     * only call the destructor when ALL references — including any permanent Mali
     * EGL reference — have been released. If Mali already released its ref the
     * wrapper's refcount is 1 (our map ref) and decRef will delete it; if Mali
     * still holds a ref decRef just decrements and Mali's eventual decRef deletes. */
    for (auto& kv : m_bufferMap) {
        OhosNativeWindowBuffer* wrapper = kv.second;
        HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
                   "freeBuffers: ohBuf=%p wrapper=%p magic=0x%08x handle=%p incRef=%p",
                   (void*)kv.first, (void*)wrapper,
                   wrapper ? wrapper->magic : 0,
                   wrapper ? (void*)wrapper->importedHandle() : nullptr,
                   wrapper ? (void*)wrapper->ANativeWindowBuffer::common.incRef : nullptr);
        /* Remove from global lookup before releasing the map-ownership reference,
         * so that any concurrent eglCreateImageKHR call won't get a stale pointer. */
        {
            std::lock_guard<std::mutex> lookup_lock(g_bufferLookupMutex);
            g_bufferLookup.erase(kv.first);
        }
        if (wrapper && wrapper->common.decRef) {
            wrapper->common.decRef(&wrapper->common);
        }
    }
    m_bufferMap.clear();
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG,
               "freeBuffers[%s]: done win=%p", caller ? caller : "?", this);
}

int OhosNativeWindow::postBuffer(ANativeWindowBuffer* buffer)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::postBuffer(buffer=%p)", buffer);

    if (!buffer) {
        HYBRIS_ERROR("Null buffer");
        return -EINVAL;
    }

    return queueBuffer((BaseNativeWindowBuffer*)buffer, -1);
}

int OhosNativeWindow::lockBuffer(BaseNativeWindowBuffer* buffer)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::lockBuffer(buffer=%p)", buffer);
    /* Deprecated in Android 4.2+; return success for compatibility. */
    return 0;
}

int OhosNativeWindow::setSwapInterval(int interval)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::setSwapInterval(interval=%d)", interval);
    return 0;
}

unsigned int OhosNativeWindow::type() const
{
    return NATIVE_WINDOW_TYPE_OHOS;
}

unsigned int OhosNativeWindow::width() const
{
    if (m_width == 0 && m_nativeWindow) {
        int32_t w = 0, h = 0;
        if (NativeWindowHandleOpt(m_nativeWindow, GET_BUFFER_GEOMETRY, &w, &h) == 0 && w > 0) {
            const_cast<OhosNativeWindow*>(this)->updateGeometry(w, h);
        }
    }
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "width() returns %d", m_width);
    return m_width;
}

unsigned int OhosNativeWindow::height() const
{
    if (m_height == 0 && m_nativeWindow) {
        int32_t w = 0, h = 0;
        if (NativeWindowHandleOpt(m_nativeWindow, GET_BUFFER_GEOMETRY, &w, &h) == 0 && h > 0) {
            const_cast<OhosNativeWindow*>(this)->updateGeometry(w, h);
        }
    }
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "height() returns %d", m_height);
    return m_height;
}

unsigned int OhosNativeWindow::format() const
{
    return OhosFormatToAndroid(m_format);
}

unsigned int OhosNativeWindow::defaultWidth() const
{
    return width();
}

unsigned int OhosNativeWindow::defaultHeight() const
{
    return height();
}

unsigned int OhosNativeWindow::queueLength() const
{
    return 0;
}

unsigned int OhosNativeWindow::transformHint() const
{
    return m_transform;
}

unsigned int OhosNativeWindow::getUsage() const
{
    return m_usage;
}

int OhosNativeWindow::setBuffersFormat(int format)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::setBuffersFormat(format=%d)", format);
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "setBuffersFormat: %d", format);
    return setBufferGeometry(m_width, m_height, format);
}

int OhosNativeWindow::setBuffersDimensions(int width, int height)
{
    HYBRIS_EGL_TRACE("OhosNativeWindow::setBuffersDimensions(width=%d, height=%d)", width, height);
    HiLogPrint(LOG_CORE, LOG_DEBUG, LOG_DOMAIN, LOG_TAG, "setBuffersDimensions: %dx%d", width, height);
    return setBufferGeometry(width, height, m_format);
}

// Factory function
ANativeWindow* createOhosNativeWindow(NativeWindow *nativeWindow)
{
    HYBRIS_EGL_TRACE("createOhosNativeWindow(nativeWindow=%p)", nativeWindow);

    if (!nativeWindow) {
        HYBRIS_ERROR("Cannot create window with null native window");
        return nullptr;
    }

    OhosNativeWindow* window = new OhosNativeWindow(nativeWindow);
    window->common.incRef(&window->common);

    return (ANativeWindow*)window;
}

// vim: noai:ts=4:sw=4:ss=4:expandtab
