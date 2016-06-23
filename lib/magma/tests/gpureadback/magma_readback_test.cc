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

#include "magma_readback_test.h"
#include <stdint.h>
#include <stdlib.h>

// framebuffer width/height
#define FB_W 64
#define FB_H 64

MagmaReadbackTest::MagmaReadbackTest() {}

bool MagmaReadbackTest::Initialize(int fd_in)
{
    if (is_initialized_)
        return false;

    fd_ = fd_in;
    curr_buf_ = 0;

    if (!InitEGL()) {
        printf("failed to initialize egl\n");
        return false;
    }
    if (!InitFramebuffer()) {
        printf("failed to initialize framebuffer\n");
        return false;
    }

    is_initialized_ = true;

    return true;
}

void MagmaReadbackTest::Cleanup()
{
    is_cleaning_up_ = true;

    CleanupFramebuffer();
    CleanupEGL();
    close(fd_);

    printf("\nCleaned Up Successfully, Exiting Cleanly\n\n");
}

MagmaReadbackTest::~MagmaReadbackTest() { Cleanup(); }

bool MagmaReadbackTest::InitEGL()
{
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        printf("could not create EGL display from default device\n");
        return false;
    }
    EGLint major, minor;
    if (!eglInitialize(display_, &major, &minor)) {
        printf("eglInitialize failed\n");
        return false;
    }
    const char* extensions = eglQueryString(display_, EGL_EXTENSIONS);
    if (!extensions) {
        printf("failed to get extension string\n");
        return false;
    }

    if (!strstr(extensions, "EGL_KHR_surfaceless_context")) {
        printf("no surfaceless support, cannot initialize\n");
        return false;
    }

    if (!(strstr(extensions, "EGL_KHR_gl_renderbuffer_image") &&
          strstr(extensions, "EGL_KHR_gl_texture_2D_image"))) {
        printf("no EGL image binding support, cannot initialize\n");
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        printf("Could not bind EGL_OPENGL_ES_API to EGL\n");
        return false;
    }

    static const EGLint kContextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context_ = eglCreateContext(display_, nullptr, EGL_NO_CONTEXT, kContextAttributes);
    if (context_ == EGL_NO_CONTEXT) {
        printf("eglCreateContext failed\n");
        return false;
    }

    if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_)) {
        printf("eglMakeCurrent failed\n");
        return false;
    }

    return true;
}

bool MagmaReadbackTest::InitFramebuffer()
{
    for (int i = 0; i < bufcount_; i++) {
        glGenFramebuffers(1, &fb_[i].fb);
        glBindFramebuffer(GL_FRAMEBUFFER, fb_[i].fb);

        glGenRenderbuffers(1, &fb_[i].color_rb);
        glBindRenderbuffer(GL_TEXTURE_2D, fb_[i].color_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB565, FB_W, FB_H);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  fb_[i].color_rb);

        glGenRenderbuffers(1, &fb_[i].depth_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, fb_[i].depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, FB_W, FB_H);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                  fb_[i].depth_rb);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("framebuffer is incomplete\n");
            return false;
        }
    }

    return true;
}

void MagmaReadbackTest::CleanupFramebuffer()
{

    for (int i = 0; i < bufcount_; i++) {
        if (fb_[i].color_rb) {
            glDeleteRenderbuffers(1, &fb_[i].color_rb);
        }

        if (fb_[i].depth_rb) {
            glDeleteRenderbuffers(1, &fb_[i].depth_rb);
        }

        if (fb_[i].fb) {
            glDeleteFramebuffers(1, &fb_[i].fb);
        }

        if (fb_[i].image) {
            eglDestroyImage(display_, fb_[i].image);
        }
    }
}

void MagmaReadbackTest::CleanupEGL()
{
    if (context_ != EGL_NO_CONTEXT) {
        eglDestroyContext(display_, context_);
    }

    if (display_ != EGL_NO_DISPLAY) {
        eglTerminate(display_);
    }
}

bool MagmaReadbackTest::Draw(void* results_buffer)
{
    if (is_cleaning_up_)
        return true;

    glBindFramebuffer(GL_FRAMEBUFFER, fb_[curr_buf_].fb);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    glReadPixels(0, 0, FB_W, FB_H, GL_RGBA, GL_UNSIGNED_BYTE, results_buffer);

    curr_buf_ = (curr_buf_ + 1) % bufcount_;

    return true;
}

extern "C" {

int test_gpu_readback(void)
{

    MagmaReadbackTest app;
    if (!app.Initialize(0xDEADBEEF)) {
        printf("could not initialize app\n");
        return -1;
    }

    // clear magenta, obviously
    glClearColor(1, 0, 1, 1);

    void* data = malloc(FB_W * FB_W * sizeof(uint32_t));
    if (!data) {
        printf("failed to allocate CPU framebuffer\n");
        return -1;
    }

    if (!app.Draw(data)) {
        printf("draw failed\n");
        return -1;
    }

    uint32_t expected_value = 0xFF00FFFF; // expect magenta, obviously
    bool has_mismatch = false;
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint32_t curr_val = ((uint32_t*)data)[i];
        if (curr_val != expected_value) {
            printf("Value Mismatch at index %d!\n\tExpected 0x%04x, got 0x%04x\n", i, curr_val,
                   expected_value);
            has_mismatch = true;
        }
    }
    if (has_mismatch) {
        printf("Test Failed!\n");
    } else {
        printf("All values matched, test passed.\n");
    }
    free(data);

    return 0;
}
}