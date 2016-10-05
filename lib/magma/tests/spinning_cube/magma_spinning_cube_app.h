// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <glm/glm.hpp>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "magma_system_display_abi.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>

#include <hid/acer12.h>

#include <chrono>

#include "spinning_cube.h"

class MagmaSpinningCubeApp {
public:
    MagmaSpinningCubeApp();
    ~MagmaSpinningCubeApp();
    bool Initialize(int fd);
    void Cleanup();
    bool Draw(float time_delta_s);
    bool CanDraw() { return !encountered_async_error_; }
    void ProcessTouchscreenInput(void* buf, size_t len);

private:
    bool InitEGL();
    void CleanupEGL();
    bool InitDisplay();
    void CleanupDisplay();
    bool InitFramebuffer();
    void CleanupFramebuffer();

    int fd_ = 0;

    struct gbm_device* gbm_ = nullptr;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;

    static const int bufcount_ = 3;
    int curr_buf_;

    struct framebuffer {
        EGLImage image = EGL_NO_IMAGE;
        struct gbm_bo* bo = nullptr;
        GLuint fb = 0, color_rb = 0, depth_rb = 0;
        uint32_t fb_handle = 0;
    };
    framebuffer fb_[bufcount_];

    SpinningCube* cube_ = nullptr;

    bool is_cleaning_up_ = false;
    bool encountered_async_error_ = false;
    bool waiting_on_page_flip_ = false;
    bool is_initialized_ = false;

    acer12_touch_t last_touch_;
    bool last_touch_valid_ = false;
    std::chrono::high_resolution_clock::time_point last_touch_timestamp_;

    magma_system_display* magma_display_ = nullptr;
};