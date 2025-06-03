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
#include <memory>
#include <mutex>
#include <vector>

// OpenHarmony includes
#include <display_type.h>
#include <window.h>

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
    
    // Buffer management
    std::vector<ANativeWindowBuffer*> m_buffers;
    uint64_t m_usage;
    int m_bufferCount;
    int m_width;
    int m_height;
    int m_format;
    int m_transform;
    int m_scalingMode;
    android_native_rect_t m_crop;
    
    void initializeDefaults();
    int allocateBuffers();
    void freeBuffers();
};

// Factory function for creating OpenHarmony native windows
ANativeWindow* createOhosNativeWindow(NativeWindow *nativeWindow);

#endif // OHOS_WINDOW_H

// vim: noai:ts=4:sw=4:ss=4:expandtab
