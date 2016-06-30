// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
    bool waiting_on_page_flip_ = false;
    bool is_initialized_ = false;
};