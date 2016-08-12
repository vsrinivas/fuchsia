// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>

class MagmaReadbackTest {
public:
    MagmaReadbackTest();
    ~MagmaReadbackTest();
    bool Initialize(int fd);
    void Cleanup();
    bool Draw(void* results_buffer);
    bool CanDraw() { return !encountered_async_error_; }

private:
    bool InitEGL();
    void CleanupEGL();
    bool InitFramebuffer();
    void CleanupFramebuffer();

    int fd_ = 0;

    struct gbm_device* gbm_ = nullptr;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;

    static const int bufcount = 1;
    int curr_buf_;

    struct framebuffer {
        EGLImage image = EGL_NO_IMAGE;
        struct gbm_bo* bo = nullptr;
        GLuint fb = 0, color_rb = 0, depth_rb = 0;
        uint32_t fb_id = 0;
    };
    framebuffer fb_[bufcount];

    bool is_cleaning_up_ = false;
    bool encountered_async_error_ = false;
    bool is_initialized_ = false;
};