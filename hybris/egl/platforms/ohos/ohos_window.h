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

#ifndef OHOS_WINDOW_H
#define OHOS_WINDOW_H

#include <android-config.h>
#include <hardware/gralloc.h>
#include "eglnativewindowbase.h"
#include "nativewindowbase.h"
#include <map>
#include <mutex>
#include <vector>

// OpenHarmony includes
#include <display_type.h>
#include <window.h>

/*
 * OhosNativeWindowBuffer — wraps an OHNativeWindowBuffer (OHOS surface buffer)
 * as an ANativeWindowBuffer for the Mali EGL driver.
 *
 * The Mali EGL driver accesses ANativeWindowBuffer::handle (native_handle_t*)
 * to obtain a DMA-buf fd for GPU memory. OHNativeWindowBuffer has a completely
 * different struct layout (C++ RefBase + sptr<SurfaceBuffer>), so a direct
 * reinterpret_cast between the two is undefined behaviour and will crash.
 *
 * Our gralloc VDI (display_buffer) stores the original buffer_handle_t pointer
 * in the last two int32_t slots of BufferHandle::reserve[] (LoadNativeHandle
 * pattern from hybris_buffer_vdi_impl.cpp).  We recover it here via
 * GetBufferHandleFromNative() + LoadNativeHandle(), then fill in the
 * ANativeWindowBuffer fields so Mali can use the buffer normally.
 *
 * Lifetime: OhosNativeWindowBuffer objects are owned by OhosNativeWindow and
 * cached in m_bufferMap keyed by OHNativeWindowBuffer*.  They live as long as
 * the window; queueBuffer / cancelBuffer return the wrapped buffer to OHOS but
 * keep the wrapper alive for reuse on the next dequeue of the same slot.
 */
class OhosNativeWindowBuffer : public BaseNativeWindowBuffer
{
public:
    explicit OhosNativeWindowBuffer(OHNativeWindowBuffer* ohBuffer);
    ~OhosNativeWindowBuffer();

    OHNativeWindowBuffer* ohBuffer() const { return m_ohBuffer; }
    buffer_handle_t importedHandle() const { return m_importedHandle; }

    /* Canary to detect double-delete via the BaseNativeWindowBuffer _decRef path */
    static constexpr uint32_t kMagicLive = 0xB00FC0DE;
    static constexpr uint32_t kMagicDead = 0xDEADBEEF;
    uint32_t magic = kMagicLive;

private:
    OHNativeWindowBuffer* m_ohBuffer;
    buffer_handle_t m_importedHandle = nullptr;
};

class OhosNativeWindow : public BaseNativeWindow
{
public:
    OhosNativeWindow(NativeWindow *nativeWindow);
    ~OhosNativeWindow();

    void lock();
    void unlock();
    void frame();
    void resize(unsigned int width, unsigned int height);

    int postBuffer(ANativeWindowBuffer* buffer);

protected:
    // Window operations
    int dequeueBuffer(BaseNativeWindowBuffer** buffer, int* fenceFd) override;
    int queueBuffer(BaseNativeWindowBuffer* buffer, int fenceFd) override;
    int cancelBuffer(BaseNativeWindowBuffer* buffer, int fenceFd) override;
    int lockBuffer(BaseNativeWindowBuffer* buffer) override;
    int setSwapInterval(int interval) override;

    // Window properties
    unsigned int type() const override;
    unsigned int width() const override;
    unsigned int height() const override;
    unsigned int format() const override;
    unsigned int defaultWidth() const override;
    unsigned int defaultHeight() const override;
    unsigned int queueLength() const override;
    unsigned int transformHint() const override;
    unsigned int getUsage() const override;

    // Query operations
    int query(int what, int* value) const;
    int perform(int operation, va_list args);

    // Buffer management
    int setUsage(uint64_t usage) override;
    int setBufferCount(int bufferCount) override;
    int setBuffersFormat(int format) override;
    int setBuffersDimensions(int width, int height) override;
    int setBufferGeometry(int width, int height, int format);
    int setScalingMode(int mode);
    int setTransform(int transform);
    int setCrop(android_native_rect_t const* rect);

private:
    NativeWindow *m_nativeWindow;
    std::mutex m_mutex;

    /*
     * Buffer cache: maps OHNativeWindowBuffer* → OhosNativeWindowBuffer*.
     * OHOS recycles a fixed pool of buffers (typically 3); we cache the
     * ANativeWindowBuffer wrapper so we don't recreate it on every frame.
     */
    std::map<OHNativeWindowBuffer*, OhosNativeWindowBuffer*> m_bufferMap;

    uint64_t m_usage;
    int m_bufferCount;
    int m_width;
    int m_height;
    int m_format;
    int m_transform;
    int m_scalingMode;
    android_native_rect_t m_crop;

    void initializeDefaults();
    void freeBuffers(const char* caller = nullptr);
    /* Update cached geometry — called from const query() via const_cast when
     * the underlying OHOS NativeWindow geometry was set after our construction. */
    void updateGeometry(int w, int h);
};

// Factory function for creating OpenHarmony native windows
ANativeWindow* createOhosNativeWindow(NativeWindow *nativeWindow);

#endif // OHOS_WINDOW_H

// vim: noai:ts=4:sw=4:ss=4:expandtab
