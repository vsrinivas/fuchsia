// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "test_spinning_cube.h"
}

#include "magma_spinning_cube_app.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include <chrono>

#include <dirent.h>
#include <errno.h>
#include <hid/acer12.h>
#include <hid/usages.h>
#include <magenta/device/input.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <stdlib.h>
#include <thread>

#include <cmath>

#define SECONDS_TO_RUN 2
#define FB_W 2160
#define FB_H 1440

#define DEV_INPUT "/dev/class/input"
#define I2C_HID_DEBUG 0

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

        int32_t fb_buffer_handle = gbm_bo_get_fd(fb_[i].bo);
        if (fb_buffer_handle < 0) {
            fprintf(stderr, "gbm_bo_get_fd returned %d\n", fb_buffer_handle);
            return false;
        }

        printf("got buffer_handle 0x%x for fb %d\n", fb_buffer_handle, i);

        magma_system_import(magma_display_, static_cast<uint32_t>(fb_buffer_handle),
                            &(fb_[i].fb_handle));

        if (magma_system_get_error(magma_display_) != 0) {
            fprintf(stderr, "magma_system_import failed\n");
            return false;
        }
  }

  return true;
}

bool MagmaSpinningCubeApp::InitDisplay()
{
    magma_display_ = magma_system_open(fd_, MAGMA_SYSTEM_CAPABILITY_DISPLAY);
    if (!magma_display_) {
        fprintf(stderr, "magma_system_display_open returned null\n");
        return false;
    }
    return true;
}

void MagmaSpinningCubeApp::CleanupDisplay() { magma_system_close(magma_display_); }

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

bool MagmaSpinningCubeApp::Draw(float time_delta_s)
{
    waiting_on_page_flip_ = false;
    if (is_cleaning_up_)
        return true;

    glBindFramebuffer(GL_FRAMEBUFFER, fb_[curr_buf_].fb);
    cube_->UpdateForTimeDelta(time_delta_s);
    cube_->Draw();
    glFinish();

    magma_system_display_page_flip(magma_display_, fb_[curr_buf_].fb_handle, &pageflip_callback,
                                   cube_);

    curr_buf_ = (curr_buf_ + 1) % bufcount_;

    waiting_on_page_flip_ = true;

    return true;
}

inline uint32_t scale32(uint32_t z, uint32_t screen_dim, uint32_t rpt_dim)
{
    return (z * screen_dim) / rpt_dim;
}

bool MagmaSpinningCubeApp::ProcessTouchscreenInput(void* buf, size_t len)
{
    if (!buf || !len) {
        last_touch_valid_ = false;
        return false;
    }

    acer12_touch_t* curr_touch = static_cast<acer12_touch_t*>(buf);
    if (len < sizeof(*curr_touch)) {
        printf("bad report size: %zd < %zd\n", len, sizeof(*curr_touch));
        return false;
    }

    // so we can use both as pointers
    auto last_touch = &last_touch_;
    bool found_finger = false;

    if (last_touch_valid_) {
        for (uint8_t curr_finger_index = 0; curr_finger_index < 5; curr_finger_index++) {
            for (uint8_t last_finger_index = 0; last_finger_index < 5; last_finger_index++) {
                auto curr_finger = &curr_touch->fingers[curr_finger_index];
                auto last_finger = &last_touch->fingers[last_finger_index];
                // find the matching finger from the last touch event;
                if (curr_finger->finger_id == last_finger->finger_id) {
                    // only proceed if touch was valid for this finger for both touch events
                    if (acer12_finger_id_tswitch(curr_finger->finger_id) &&
                        acer12_finger_id_tswitch(last_finger->finger_id)) {

                        glm::vec2 start(scale32(last_finger->x, FB_W, ACER12_X_MAX),
                                        scale32(last_finger->y, FB_H, ACER12_Y_MAX));
                        glm::vec2 end(scale32(curr_finger->x, FB_W, ACER12_X_MAX),
                                      scale32(curr_finger->y, FB_H, ACER12_Y_MAX));

                        found_finger = cube_->UpdateForDragVector(start, end, (float)(curr_touch->scan_time - last_touch->scan_time) / 10000.f);
                    }
                    break;
                }
            }
            if (found_finger)
                break;
        }
    }

    memcpy(last_touch, curr_touch, sizeof(acer12_touch_t));
    last_touch_valid_ = true;
    last_touch_timestamp_ = std::chrono::high_resolution_clock::now();

    return found_finger;
}

extern "C" {

int test_spinning_cube()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));

    int fd = open("/dev/class/display/000", O_RDONLY);
    if (fd < 0) {
        printf("failed to open gpu %d", fd);
        return -1;
    }

    // Scan /dev/class/input to find the touchscreen
    struct dirent* de;
    DIR* dir = opendir(DEV_INPUT);
    if (!dir) {
        printf("failed to open %s: %d\n", DEV_INPUT, errno);
        return -1;
    }

    int ret = 0;
    int touchfd = -1;
    size_t rpt_desc_len = 0;
    uint8_t* rpt_desc = NULL;
    while ((de = readdir(dir)) != NULL) {
        char devname[128];
        snprintf(devname, sizeof(devname), "%s/%s", DEV_INPUT, de->d_name);
        touchfd = open(devname, O_RDONLY);
        if (touchfd < 0) {
            printf("failed to open %s: %d\n", devname, errno);
            continue;
        }

        ret = ioctl_input_get_report_desc_size(touchfd, &rpt_desc_len);
        if (ret < 0) {
            printf("failed to get report descriptor length for %s: %zd\n", devname, ret);
            touchfd = -1;
            continue;
        }
        if (rpt_desc_len != ACER12_RPT_DESC_LEN) {
            rpt_desc_len = 0;
            touchfd = -1;
            continue;
        }

        rpt_desc = (uint8_t*)malloc(rpt_desc_len);
        if (rpt_desc == NULL) {
            printf("no memory!\n");
            return -1;
        }
        ret = ioctl_input_get_report_desc(touchfd, rpt_desc, rpt_desc_len);
        if (ret < 0) {
            printf("failed to get report descriptor for %s: %zd\n", devname, ret);
            rpt_desc_len = 0;
            free(rpt_desc);
            rpt_desc = NULL;
            touchfd = -1;
            continue;
        }
        if (!memcmp(rpt_desc, acer12_touch_report_desc, ACER12_RPT_DESC_LEN)) {
            // Found the touchscreen
            printf("touchscreen: %s\n", devname);
            break;
        }
        touchfd = -1;
    }
    closedir(dir);

    if (touchfd < 0) {
        printf("could not find a touchscreen!\n");
        return -1;
    }
    assert(rpt_desc_len > 0);
    assert(rpt_desc);

    input_report_size_t max_rpt_sz = 0;
    ret = ioctl_input_get_max_reportsize(touchfd, &max_rpt_sz);
    if (ret < 0) {
        printf("failed to get max report size: %zd\n", ret);
        return -1;
    }
    void* buf = malloc(max_rpt_sz);
    if (buf == NULL) {
        printf("no memory!\n");
        return -1;
    }

    MagmaSpinningCubeApp app;
    if (!app.Initialize(fd)) {
        return -1;
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    uint32_t num_frames = 60;
    uint32_t frame_count = 0;
    uint64_t total_milliseconds = 0;
    ssize_t r;
    static const float milliseconds_per_second =
        std::chrono::milliseconds(std::chrono::seconds(1)).count();
    while (true) {

        uint32_t num_events = 0;

        bool updated_for_input = false;
        while (mxio_wait_fd(touchfd, MXIO_EVT_READABLE, NULL, 0) == 0) {
            r = read(touchfd, buf, max_rpt_sz);
            if (r < 0) {
                printf("touchscreen read error: %zd\n", r);
                break;
            }
            if (*(uint8_t*)buf == ACER12_RPT_ID_TOUCH) {
                num_events++;
            }
        }

        if (num_events > 0) {
            updated_for_input = app.ProcessTouchscreenInput(buf, r);
        } else {
            app.ProcessTouchscreenInput(nullptr, 0);
        }

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

        if (!app.Draw(updated_for_input ? 0.0 : std::chrono::duration<float>(t1 - t0).count())) {
            break;
        }

        t0 = t1;
    }

    return 0;
}
}