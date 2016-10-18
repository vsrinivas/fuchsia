// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_readback_test.h"
#include <stdint.h>
#include <stdlib.h>

// framebuffer width/height
#define FB_W 64
#define FB_H 64

MagmaReadbackTest::MagmaReadbackTest() {}

bool MagmaReadbackTest::Initialize(int fd)
{
    if (is_initialized_)
        return false;

    fd_ = fd;
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

    printf("\nCleaned Up Successfully, Exiting Cleanly\n\n");
}

MagmaReadbackTest::~MagmaReadbackTest() { Cleanup(); }

bool MagmaReadbackTest::InitEGL()
{
    gbm_ = gbm_create_device(fd_);
    if (!gbm_) {
        printf("could not create gbm_device\n");
        return false;
    }
    printf("GBM device created\n");

    display_ = eglGetDisplay(gbm_);
    if (display_ == EGL_NO_DISPLAY) {
        printf("could not create EGL display\n");
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

    extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));

    if (!(strstr(extensions, "GL_OES_rgb8_rgba8"))) {
        printf("extension GL_OES_rgb8_rgba8 not found:\n%s\n", extensions);
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
        glBindRenderbuffer(GL_RENDERBUFFER, fb_[i].color_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, FB_W, FB_H);
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

    if (gbm_)
        gbm_device_destroy(gbm_);
}

bool MagmaReadbackTest::Draw(void* results_buffer)
{
    if (is_cleaning_up_)
        return true;

    glBindFramebuffer(GL_FRAMEBUFFER, fb_[curr_buf_].fb);
    glClear(GL_COLOR_BUFFER_BIT);

    printf("->glFinish\n");
    glFinish();

    printf("->glReadPixels\n");
    glReadPixels(0, 0, FB_W, FB_H, GL_RGBA, GL_UNSIGNED_BYTE, results_buffer);

    curr_buf_ = (curr_buf_ + 1) % bufcount_;

    return true;
}

extern "C" {

int test_gpu_readback()
{
    int fd = open("/dev/class/display/000", O_RDONLY);
    if (fd < 0) {
        printf("could not open gpu: %d\n", fd);
        return -1;
    }

    MagmaReadbackTest app;
    if (!app.Initialize(fd)) {
        printf("could not initialize app\n");
        return -1;
    }

    glClearColor(1, 0, 0.5, 0.75);

    int size = FB_W * FB_H * sizeof(uint32_t);
    auto data = reinterpret_cast<uint32_t*>(malloc(size));
    if (!data) {
        printf("failed to allocate CPU framebuffer\n");
        return -1;
    }

    memset(data, 0, size);

    if (!app.Draw(data)) {
        printf("draw failed\n");
        return -1;
    }

    printf("Draw complete, comparing\n");

    uint32_t expected_value = 0xBF8000FF;
    int mismatches = 0;
    for (int i = 0; i < FB_W * FB_H; i++) {
        if (data[i] != expected_value) {
            if (mismatches++ < 10)
                printf("Value Mismatch at index %d - expected 0x%04x, got 0x%04x\n", i,
                       expected_value, data[i]);
        }
    }
    if (mismatches) {
        printf("****** Test Failed! %d mismatches\n", mismatches);
    } else {
        printf("****** Test Passed! All values matched.\n");
    }
    free(data);

    return 0;
}
}
