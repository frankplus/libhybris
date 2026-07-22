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

/* ─── Recover native_handle_t* from BufferHandle::reserve[] ────────────────
 * Our gralloc VDI (hybris_buffer_vdi_impl.cpp) stores the original
 * buffer_handle_t pointer (allocated by hybris_gralloc_allocate) in the last
 * two int32_t slots of BufferHandle::reserve[].  Recover it so the Mali EGL
 * driver can access the DMA-buf fd via ANativeWindowBuffer::handle.
 */
static constexpr uint32_t kPtrSlots = 2;

static buffer_handle_t LoadNativeHandleFromBH(const BufferHandle& bh)
{
    uint32_t offset = bh.reserveFds + bh.reserveInts - kPtrSlots;
    uintptr_t lo = static_cast<uint32_t>(bh.reserve[offset]);
    uintptr_t hi = static_cast<uint32_t>(bh.reserve[offset + 1]);
    return reinterpret_cast<buffer_handle_t>(lo | (hi << 32u));
}

/* ─── OhosNativeWindowBuffer ─────────────────────────────────────────────── */

OhosNativeWindowBuffer::OhosNativeWindowBuffer(OHNativeWindowBuffer* ohBuffer)
    : BaseNativeWindowBuffer()
    , m_ohBuffer(ohBuffer)
{
    TRACE("OhosNativeWindowBuffer(%p)", ohBuffer);

    NativeObjectReference(ohBuffer);

    BufferHandle* bh = GetBufferHandleFromNative(ohBuffer);
    if (!bh) {
        HYBRIS_ERROR("GetBufferHandleFromNative returned null for %p", ohBuffer);
        return;
    }

    buffer_handle_t nh = LoadNativeHandleFromBH(*bh);
    if (!nh) {
        HYBRIS_ERROR("LoadNativeHandleFromBH returned null for ohBuffer %p", ohBuffer);
        return;
    }

    ANativeWindowBuffer::handle  = nh;
    ANativeWindowBuffer::width   = bh->width;
    ANativeWindowBuffer::height  = bh->height;
    ANativeWindowBuffer::stride  = bh->stride;
    ANativeWindowBuffer::format  = OhosFormatToAndroid(bh->format);
    ANativeWindowBuffer::usage   = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;

    TRACE("OhosNativeWindowBuffer: %dx%d stride=%d fmt=%d handle=%p",
          bh->width, bh->height, bh->stride, bh->format, nh);
}

OhosNativeWindowBuffer::~OhosNativeWindowBuffer()
{
    TRACE("~OhosNativeWindowBuffer(%p)", m_ohBuffer);
    if (m_ohBuffer) {
        NativeObjectUnreference(m_ohBuffer);
        m_ohBuffer = nullptr;
    }
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
    TRACE("OhosNativeWindow::OhosNativeWindow(nativeWindow=%p)", nativeWindow);

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
    }

    initializeDefaults();
}

OhosNativeWindow::~OhosNativeWindow()
{
    TRACE("OhosNativeWindow::~OhosNativeWindow()");

    freeBuffers();

    if (m_nativeWindow) {
        NativeObjectUnreference(m_nativeWindow);
        m_nativeWindow = nullptr;
    }
}

void OhosNativeWindow::initializeDefaults()
{
    m_crop = {0, 0, m_width, m_height};
    m_usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;
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
    TRACE("OhosNativeWindow::resize(width=%u, height=%u)", width, height);

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_width != (int)width || m_height != (int)height) {
        m_width = width;
        m_height = height;
        m_crop = {0, 0, (int32_t)width, (int32_t)height};
        freeBuffers(); /* drop cached wrappers; new ones created on next dequeue */
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
    TRACE("OhosNativeWindow::dequeueBuffer()");

    if (!m_nativeWindow) {
        HYBRIS_ERROR("Native window is null");
        return -EINVAL;
    }

    OHNativeWindowBuffer* ohBuffer = nullptr;
    int fence = -1;

    int result = NativeWindowRequestBuffer(m_nativeWindow, &ohBuffer, &fence);
    if (result != 0) {
        HYBRIS_ERROR("NativeWindowRequestBuffer failed: %d", result);
        return result;
    }
    if (!ohBuffer) {
        HYBRIS_ERROR("NativeWindowRequestBuffer returned null buffer");
        return -EINVAL;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_bufferMap.find(ohBuffer);
    OhosNativeWindowBuffer* wrapper;
    if (it == m_bufferMap.end()) {
        wrapper = new OhosNativeWindowBuffer(ohBuffer);
        m_bufferMap[ohBuffer] = wrapper;
    } else {
        wrapper = it->second;
    }

    *buffer = wrapper;
    *fenceFd = fence;

    TRACE("OhosNativeWindow::dequeueBuffer() = wrapper %p (ohBuffer %p) fence=%d",
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
    TRACE("OhosNativeWindow::queueBuffer(buffer=%p, fenceFd=%d)", buffer, fenceFd);

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
    TRACE("OhosNativeWindow::cancelBuffer(buffer=%p, fenceFd=%d)", buffer, fenceFd);

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
    TRACE("OhosNativeWindow::query(what=%d)", what);

    if (!value) {
        return -EINVAL;
    }

    switch (what) {
        case NATIVE_WINDOW_WIDTH:
            *value = m_width;
            return 0;
        case NATIVE_WINDOW_HEIGHT:
            *value = m_height;
            return 0;
        case NATIVE_WINDOW_FORMAT:
            *value = m_format;
            return 0;
        case NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS:
            *value = 1;
            return 0;
        case NATIVE_WINDOW_CONSUMER_USAGE_BITS:
            *value = 0;
            return 0;
        case NATIVE_WINDOW_TRANSFORM_HINT:
            *value = m_transform;
            return 0;
        case NATIVE_WINDOW_CONCRETE_TYPE:
            *value = NATIVE_WINDOW_TYPE_OHOS;
            return 0;
        case NATIVE_WINDOW_DEFAULT_WIDTH:
            *value = m_width;
            return 0;
        case NATIVE_WINDOW_DEFAULT_HEIGHT:
            *value = m_height;
            return 0;
        default:
            HYBRIS_ERROR("Unknown query parameter: %d", what);
            return -EINVAL;
    }
}

int OhosNativeWindow::perform(int operation, va_list args)
{
    TRACE("OhosNativeWindow::perform(operation=%d)", operation);

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
    TRACE("OhosNativeWindow::setUsage(usage=0x%" PRIx64 ")", usage);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_usage = usage;

    if (m_nativeWindow) {
        return NativeWindowHandleOpt(m_nativeWindow, SET_USAGE, usage);
    }
    return 0;
}

int OhosNativeWindow::setBufferCount(int bufferCount)
{
    TRACE("OhosNativeWindow::setBufferCount(bufferCount=%d)", bufferCount);

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
    TRACE("OhosNativeWindow::setBufferGeometry(width=%d, height=%d, format=%d)", width, height, format);

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
    TRACE("OhosNativeWindow::setScalingMode(mode=%d)", mode);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scalingMode = mode;
    return 0;
}

int OhosNativeWindow::setTransform(int transform)
{
    TRACE("OhosNativeWindow::setTransform(transform=%d)", transform);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_transform = transform;
    return 0;
}

int OhosNativeWindow::setCrop(android_native_rect_t const* rect)
{
    TRACE("OhosNativeWindow::setCrop(rect=%p)", rect);

    if (!rect) {
        HYBRIS_ERROR("Null crop rectangle");
        return -EINVAL;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_crop = *rect;
    return 0;
}

void OhosNativeWindow::freeBuffers()
{
    /* Called under m_mutex or from destructor. Delete all cached wrappers. */
    for (auto& kv : m_bufferMap) {
        delete kv.second;
    }
    m_bufferMap.clear();
}

int OhosNativeWindow::postBuffer(ANativeWindowBuffer* buffer)
{
    TRACE("OhosNativeWindow::postBuffer(buffer=%p)", buffer);

    if (!buffer) {
        HYBRIS_ERROR("Null buffer");
        return -EINVAL;
    }

    return queueBuffer((BaseNativeWindowBuffer*)buffer, -1);
}

int OhosNativeWindow::lockBuffer(BaseNativeWindowBuffer* buffer)
{
    TRACE("OhosNativeWindow::lockBuffer(buffer=%p)", buffer);
    /* Deprecated in Android 4.2+; return success for compatibility. */
    return 0;
}

int OhosNativeWindow::setSwapInterval(int interval)
{
    TRACE("OhosNativeWindow::setSwapInterval(interval=%d)", interval);
    return 0;
}

unsigned int OhosNativeWindow::type() const
{
    return NATIVE_WINDOW_TYPE_OHOS;
}

unsigned int OhosNativeWindow::width() const
{
    return m_width;
}

unsigned int OhosNativeWindow::height() const
{
    return m_height;
}

unsigned int OhosNativeWindow::format() const
{
    return m_format;
}

unsigned int OhosNativeWindow::defaultWidth() const
{
    return m_width;
}

unsigned int OhosNativeWindow::defaultHeight() const
{
    return m_height;
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
    TRACE("OhosNativeWindow::setBuffersFormat(format=%d)", format);
    return setBufferGeometry(m_width, m_height, format);
}

int OhosNativeWindow::setBuffersDimensions(int width, int height)
{
    TRACE("OhosNativeWindow::setBuffersDimensions(width=%d, height=%d)", width, height);
    return setBufferGeometry(width, height, m_format);
}

// Factory function
ANativeWindow* createOhosNativeWindow(NativeWindow *nativeWindow)
{
    TRACE("createOhosNativeWindow(nativeWindow=%p)", nativeWindow);

    if (!nativeWindow) {
        HYBRIS_ERROR("Cannot create window with null native window");
        return nullptr;
    }

    OhosNativeWindow* window = new OhosNativeWindow(nativeWindow);
    window->common.incRef(&window->common);

    return (ANativeWindow*)window;
}

// vim: noai:ts=4:sw=4:ss=4:expandtab
