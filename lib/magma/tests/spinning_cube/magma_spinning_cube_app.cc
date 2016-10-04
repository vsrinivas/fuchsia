// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_spinning_cube_app.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include <chrono>

#define SECONDS_TO_RUN 2
#define FB_W 2160
#define FB_H 1440

MagmaSpinningCubeApp::MagmaSpinningCubeApp() {}

static void pageflip_callback(int32_t error, void* data)
{
    if (error) {
        DLOG("magma_system_page_flip failed");
        DASSERT(false);
    }
}

bool MagmaSpinningCubeApp::Initialize(int fd_in) {
  if( is_initialized_ )
    return false;

  fd_ = fd_in;
  curr_buf_ = 0;

  if (!InitEGL())
    return false;
  if (!InitDisplay())
      return false;
  if (!InitFramebuffer())
    return false;

  cube_ = new SpinningCube();
  cube_->Init();
  cube_->set_size(FB_W, FB_H);
  cube_->set_color(1.0, 1.0, 1.0);
  cube_->UpdateForTimeDelta(0);

  is_initialized_ = true;

  return true;
}

void MagmaSpinningCubeApp::Cleanup(){
  is_cleaning_up_ = true;

  delete cube_;
  CleanupFramebuffer();
  CleanupEGL();
  CleanupDisplay();

  fprintf(stderr, "\nCLeaned Up Successfully, Exiting Cleanly\n\n");
}

MagmaSpinningCubeApp::~MagmaSpinningCubeApp() {
  Cleanup();
}

bool MagmaSpinningCubeApp::InitEGL() {
    gbm_ = gbm_create_device(fd_);
    if (!gbm_) {
        perror("could not create gbm_device");
        return false;
  }
  display_ = eglGetDisplay(gbm_);
  if (display_ == EGL_NO_DISPLAY) {
    perror("could not create EGL display from gbm device");
    return false;
  }
  EGLint major, minor;
  if (!eglInitialize(display_, &major, &minor)) {
    fprintf(stderr, "eglInitialize failed\n");
    return false;
  }
  const char *extensions = eglQueryString(display_, EGL_EXTENSIONS);
  if (!extensions) {
    fprintf(stderr, "failed to get extension string\n");
    return false;
  }

  if (!strstr(extensions, "EGL_KHR_surfaceless_context")) {
    fprintf(stderr, "no surfaceless support, cannot initialize\n");
    return false;
  }

  if (!(strstr(extensions, "EGL_KHR_gl_renderbuffer_image") &&
        strstr(extensions, "EGL_KHR_gl_texture_2D_image"))) {
    fprintf(stderr, "no EGL image binding support, cannot initialize\n");
    return false;
  }

  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    fprintf(stderr, "Could not bind EGL_OPENGL_ES_API to EGL\n");
    return false;
  }

  static const EGLint kContextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                              EGL_NONE};
  EGLContext context_ =
      eglCreateContext(display_, nullptr, EGL_NO_CONTEXT, kContextAttributes);
  if (context_ == EGL_NO_CONTEXT) {
    fprintf(stderr, "eglCreateContext failed\n");
    return false;
  }

  if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_)) {
    fprintf(stderr, "eglMakeCurrent failed\n");
    return false;
  }

  return true;
}

bool MagmaSpinningCubeApp::InitFramebuffer() {
    // auto egl_image_target_renderbuffer_storage =
    //     reinterpret_cast<PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC>(
    //         eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES"));

    auto egl_image_target_texture_2d = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));

    for (int i = 0; i < bufcount_; i++) {
        fb_[i].bo = gbm_bo_create(gbm_, FB_W, FB_H, GBM_BO_FORMAT_ARGB8888,
                                  GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (!fb_[i].bo) {
            perror("could not create GBM buffer");
            return false;
        }
        // uint32_t stride = gbm_bo_get_stride(fb_[i].bo);

        fb_[i].image =
            eglCreateImage(display_, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, fb_[i].bo, nullptr);
        if (!fb_[i].image) {
            fprintf(stderr, "eglCreateImage returned null\n");
            return false;
        }

        glGenFramebuffers(1, &fb_[i].fb);
        glBindFramebuffer(GL_FRAMEBUFFER, fb_[i].fb);

        glGenTextures(1, &fb_[i].color_rb);
        glBindTexture(GL_TEXTURE_2D, fb_[i].color_rb);
        egl_image_target_texture_2d(GL_TEXTURE_2D, fb_[i].image);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_[i].color_rb,
                               0);

        glGenRenderbuffers(1, &fb_[i].depth_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, fb_[i].depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, FB_W, FB_H);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                  fb_[i].depth_rb);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "framebuffer is incomplete\n");
            return false;
        }

        int32_t fb_token = gbm_bo_get_fd(fb_[i].bo);
        if (fb_token < 0) {
            fprintf(stderr, "gbm_bo_get_fd returned %d\n", fb_token);
            return false;
        }

        printf("got token 0x%x for fb %d\n", fb_token, i);

        if (!magma_system_display_import_buffer(magma_display_, static_cast<uint32_t>(fb_token),
                                                &(fb_[i].fb_handle))) {
            fprintf(stderr, "magma_system_display_import_buffer failed\n");
            return false;
        }
  }

  return true;
}

bool MagmaSpinningCubeApp::InitDisplay()
{
    magma_display_ = magma_system_display_open(fd_);
    if (!magma_display_) {
        fprintf(stderr, "magma_system_display_open returned null\n");
        return false;
    }
    return true;
}

void MagmaSpinningCubeApp::CleanupDisplay() { magma_system_display_close(magma_display_); }

void MagmaSpinningCubeApp::CleanupFramebuffer() {

  for (int i = 0; i < bufcount_; i++) {

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

void MagmaSpinningCubeApp::CleanupEGL() {
  if (context_ != EGL_NO_CONTEXT) {
    eglDestroyContext(display_, context_);
  }

  if (display_ != EGL_NO_DISPLAY) {
    eglTerminate(display_);
  }
  if (gbm_) {
    gbm_device_destroy(gbm_);
  }
}

bool MagmaSpinningCubeApp::Draw(uint32_t time_delta_ms)
{
    waiting_on_page_flip_ = false;
    if (is_cleaning_up_)
        return true;

    glBindFramebuffer(GL_FRAMEBUFFER, fb_[curr_buf_].fb);
    cube_->UpdateForTimeDelta(time_delta_ms / 1000.f);
    cube_->Draw();
    glFinish();

    uint32_t prev_buf = (curr_buf_ + bufcount_ - 1) % bufcount_;
    magma_system_display_page_flip(magma_display_, fb_[prev_buf].fb_handle, &pageflip_callback,
                                   cube_);

    curr_buf_ = (curr_buf_ + 1) % bufcount_;

    waiting_on_page_flip_ = true;

    return true;
}

extern "C" {

int test_spinning_cube(uint32_t device_handle)
{

    MagmaSpinningCubeApp app;
    if (!app.Initialize(device_handle)) {
        return -1;
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    uint32_t num_frames = 60;
    uint32_t frame_count = 0;
    uint64_t total_milliseconds = 0;
    static const float milliseconds_per_second =
        std::chrono::milliseconds(std::chrono::seconds(1)).count();
    while (true) {

        auto t1 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = t1 - t0;
        double milliseconds = elapsed.count();
        total_milliseconds += milliseconds;
        if (frame_count++ > num_frames) {
            printf("Framerate average for last %u frames: %f frames per second\n", num_frames,
                   (num_frames) / (total_milliseconds / milliseconds_per_second));
            frame_count = 0;
            total_milliseconds = 0;
        }

        if (!app.Draw(milliseconds)) {
            break;
        }

        t0 = t1;
    }

    return 0;
}
}