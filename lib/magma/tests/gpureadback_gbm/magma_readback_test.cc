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
    printf("InitEGL SUCCESS\n");

    if (!InitFramebuffer()) {
        printf("failed to initialize framebuffer\n");
        return false;
    }

    printf("InitFramebuffer SUCCESS\n");

    is_initialized_ = true;

    return true;
}

void MagmaReadbackTest::Cleanup()
{
    is_cleaning_up_ = true;

    CleanupFramebuffer();
    CleanupEGL();

    printf("\nCLeaned Up Successfully, Exiting Cleanly\n\n");
}

MagmaReadbackTest::~MagmaReadbackTest() { Cleanup(); }

bool MagmaReadbackTest::InitEGL()
{
    printf("MagmaReadbackTest::InitEGL()\n");

    gbm_ = gbm_create_device(fd_);
    if (!gbm_) {
        printf("could not create gbm_device\n");
        return false;
    }
    printf("GBM device created\n");

    display_ = eglGetDisplay(gbm_);
    if (display_ == EGL_NO_DISPLAY) {
        printf("could not create EGL display from gbm device\n");
        return false;
    }
    printf("Got EGL display\n");

    EGLint major, minor;
    if (!eglInitialize(display_, &major, &minor)) {
        printf("eglInitialize failed\n");
        return false;
    }
    printf("EGL intialized\n");

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
    auto egl_image_target_texture_2d = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));

    printf("InitFramebuffer start\n");

    for (int i = 0; i < bufcount; i++) {
        fb_[i].bo = gbm_bo_create(gbm_, FB_W, FB_H, GBM_BO_FORMAT_ARGB8888,
                                  GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (!fb_[i].bo) {
            printf("could not create GBM buffer");
            return false;
        }
        printf("Got bo %p\n", fb_[i].bo);

        fb_[i].image =
            eglCreateImage(display_, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, fb_[i].bo, nullptr);
        if (!fb_[i].image) {
            printf("eglCreateImage returned null\n");
            return false;
        }
        printf("InitFramebuffer got image %p\n", fb_[i].image);

        glGenFramebuffers(1, &fb_[i].fb);
        printf("InitFramebuffer got fb %d\n", fb_[i].fb);

        glBindFramebuffer(GL_FRAMEBUFFER, fb_[i].fb);

        glGenTextures(1, &fb_[i].color_rb);
        printf("InitFramebuffer got texture %d\n", fb_[i].color_rb);

        glBindTexture(GL_TEXTURE_2D, fb_[i].color_rb);
        printf("calling egl_image_target_texture_2d %p\n", egl_image_target_texture_2d);
        egl_image_target_texture_2d(GL_TEXTURE_2D, fb_[i].image);
        printf("InitFramebuffer egl_image_target_texture_2d returned\n");
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_[i].color_rb,
                               0);

        glGenRenderbuffers(1, &fb_[i].depth_rb);
        printf("InitFramebuffer got renderbuffer %d\n", fb_[i].depth_rb);

        glBindRenderbuffer(GL_RENDERBUFFER, fb_[i].depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, FB_W, FB_H);
        printf("InitFramebuffer calling glFramebufferRenderbuffer\n");
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                  fb_[i].depth_rb);
        printf("InitFramebuffer returned from glFramebufferRenderbuffer\n");

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("framebuffer is incomplete\n");
            return false;
        }

        printf("InitFramebuffer FB is complete\n");
    }

    printf("InitFramebuffer finish\n");

    return true;
}

void MagmaReadbackTest::CleanupFramebuffer()
{

    for (int i = 0; i < bufcount; i++) {
        if (fb_[i].color_rb) {
            glDeleteTextures(1, &fb_[i].color_rb);
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

        if (fb_[i].bo) {
            gbm_bo_destroy(fb_[i].bo);
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
    glFinish();

    glReadPixels(0, 0, FB_W, FB_H, GL_RGBA, GL_UNSIGNED_BYTE, results_buffer);

    curr_buf_ = (curr_buf_ + 1) % bufcount;

    return true;
}

extern "C" {

int test_gpu_readback()
{
    int fd = open("/dev/class/display/000", O_RDONLY);
    if (fd < 0) {
        printf("could not open gpu device %d\n", fd);
        return -1;
    }

    MagmaReadbackTest app;
    if (!app.Initialize(fd)) {
        printf("could not initialize app\n");
        return -1;
    }

    printf("app::Initialize SUCCESS\n");

    glClearColor(1, 0, 0.5, 0.75);

    int size = FB_W * FB_H * sizeof(uint32_t);
    auto data = reinterpret_cast<uint32_t*>(malloc(size));
    if (!data) {
        printf("failed to allocate CPU framebuffer\n");
        return -1;
    }

    if (!app.Draw(data)) {
        printf("draw failed\n");
        return -1;
    }

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