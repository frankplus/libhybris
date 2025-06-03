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

OhosNativeWindow::OhosNativeWindow(NativeWindow *nativeWindow)
    : BaseNativeWindow()
    , m_nativeWindow(nativeWindow)
    , m_usage(0)
    , m_bufferCount(3) // Default to triple buffering
    , m_width(0)
    , m_height(0)
    , m_format(PIXEL_FMT_RGBA_8888)
    , m_transform(0)
    , m_scalingMode(NATIVE_WINDOW_SCALING_MODE_FREEZE)
{
    TRACE("OhosNativeWindow::OhosNativeWindow(nativeWindow=%p)", nativeWindow);
    
    if (nativeWindow) {
        // Acquire reference to the native window
        NativeObjectReference(nativeWindow);
        
        // Query initial window properties
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
    // Initialize crop rectangle to full window
    m_crop = {0, 0, m_width, m_height};
    
    // Set default usage flags for GPU rendering
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
    // No-op for OpenHarmony - frame synchronization handled by native window
}

void OhosNativeWindow::resize(unsigned int width, unsigned int height)
{
    TRACE("OhosNativeWindow::resize(width=%u, height=%u)", width, height);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_width != (int)width || m_height != (int)height) {
        m_width = width;
        m_height = height;
        m_crop = {0, 0, (int32_t)width, (int32_t)height};
        
        // Reallocate buffers with new dimensions
        freeBuffers();
        allocateBuffers();
    }
}

int OhosNativeWindow::dequeueBuffer(BaseNativeWindowBuffer** buffer, int* fenceFd)
{
    TRACE("OhosNativeWindow::dequeueBuffer()");
    
    if (!m_nativeWindow) {
        HYBRIS_ERROR("Native window is null");
        return -EINVAL;
    }
    
    OHNativeWindowBuffer* ohBuffer = nullptr;
    int fence = -1;
    
    // Request buffer from OpenHarmony native window
    int result = NativeWindowRequestBuffer(m_nativeWindow, &ohBuffer, &fence);
    if (result != 0) {
        HYBRIS_ERROR("Failed to request buffer from native window: %d", result);
        return result;
    }
    
    if (!ohBuffer) {
        HYBRIS_ERROR("Received null buffer from native window");
        return -EINVAL;
    }
    
    // Cast OHNativeWindowBuffer to BaseNativeWindowBuffer
    // This works because both are essentially buffer wrappers
    *buffer = reinterpret_cast<BaseNativeWindowBuffer*>(ohBuffer);
    *fenceFd = fence;
    
    TRACE("OhosNativeWindow::dequeueBuffer() = %p, fence=%d", ohBuffer, fence);
    return 0;
}

int OhosNativeWindow::queueBuffer(BaseNativeWindowBuffer* buffer, int fenceFd)
{
    TRACE("OhosNativeWindow::queueBuffer(buffer=%p, fenceFd=%d)", buffer, fenceFd);
    
    if (!m_nativeWindow || !buffer) {
        HYBRIS_ERROR("Invalid parameters: nativeWindow=%p, buffer=%p", m_nativeWindow, buffer);
        return -EINVAL;
    }
    
    // Cast BaseNativeWindowBuffer back to OHNativeWindowBuffer for OpenHarmony API
    OHNativeWindowBuffer* ohBuffer = reinterpret_cast<OHNativeWindowBuffer*>(buffer);
    
    // Create dirty region - convert Android rect to OpenHarmony Region
    Region dirty;
    Region::Rect ohRect;
    ohRect.x = m_crop.left;
    ohRect.y = m_crop.top;
    ohRect.w = static_cast<uint32_t>(m_crop.right - m_crop.left);
    ohRect.h = static_cast<uint32_t>(m_crop.bottom - m_crop.top);
    
    dirty.rectNumber = 1;
    dirty.rects = &ohRect;
    
    // Queue the buffer to the OpenHarmony native window
    int result = NativeWindowFlushBuffer(m_nativeWindow, ohBuffer, fenceFd, dirty);
    if (result != 0) {
        HYBRIS_ERROR("Failed to flush buffer to native window: %d", result);
    }
    
    return result;
}

int OhosNativeWindow::cancelBuffer(BaseNativeWindowBuffer* buffer, int fenceFd)
{
    TRACE("OhosNativeWindow::cancelBuffer(buffer=%p, fenceFd=%d)", buffer, fenceFd);
    
    if (!m_nativeWindow || !buffer) {
        HYBRIS_ERROR("Invalid parameters: nativeWindow=%p, buffer=%p", m_nativeWindow, buffer);
        return -EINVAL;
    }
    
    // Cast BaseNativeWindowBuffer back to OHNativeWindowBuffer for OpenHarmony API
    OHNativeWindowBuffer* ohBuffer = reinterpret_cast<OHNativeWindowBuffer*>(buffer);
    
    // Cancel the buffer in the OpenHarmony native window
    int result = NativeWindowCancelBuffer(m_nativeWindow, ohBuffer);
    if (result != 0) {
        HYBRIS_ERROR("Failed to cancel buffer in native window: %d", result);
    }
    
    // Close fence if valid
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
            *value = 1; // OpenHarmony requires at least 1 buffer to stay queued
            return 0;
            
        case NATIVE_WINDOW_CONSUMER_USAGE_BITS:
            *value = 0; // No specific consumer usage requirements
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

int OhosNativeWindow::allocateBuffers()
{
    // Buffer allocation is handled by the OpenHarmony native window system
    // We don't need to pre-allocate buffers here
    return 0;
}

void OhosNativeWindow::freeBuffers()
{
    // Buffer cleanup is handled by the OpenHarmony native window system
    m_buffers.clear();
}

int OhosNativeWindow::postBuffer(ANativeWindowBuffer* buffer)
{
    TRACE("OhosNativeWindow::postBuffer(buffer=%p)", buffer);
    
    if (!buffer) {
        HYBRIS_ERROR("Null buffer");
        return -EINVAL;
    }
    
    // Use queueBuffer with no fence
    return queueBuffer((BaseNativeWindowBuffer*)buffer, -1);
}

int OhosNativeWindow::lockBuffer(BaseNativeWindowBuffer* buffer)
{
    TRACE("OhosNativeWindow::lockBuffer(buffer=%p)", buffer);
    // Lock buffer operation is deprecated in modern Android
    // Return success for compatibility
    return 0;
}

int OhosNativeWindow::setSwapInterval(int interval)
{
    TRACE("OhosNativeWindow::setSwapInterval(interval=%d)", interval);
    // OpenHarmony doesn't directly support swap interval setting
    // Return success for compatibility
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
    return 0; // OpenHarmony doesn't queue to window composer
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
