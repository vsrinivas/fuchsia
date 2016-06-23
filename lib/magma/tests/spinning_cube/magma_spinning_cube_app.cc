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

#include "magma_spinning_cube_app.h"

#define SECONDS_TO_RUN 2

MagmaSpinningCubeApp::MagmaSpinningCubeApp() {}

static void modeset_callback(int fd, unsigned int frame, unsigned int sec,
                      unsigned int usec, void *data) {
  MagmaSpinningCubeApp *app = static_cast<MagmaSpinningCubeApp *>(data);
  if (!app->Draw(16)) {
    fprintf(stderr, "draw failed\n");
  }
}

bool MagmaSpinningCubeApp::Initialize(int fd_in) {
  if( is_initialized_ )
    return false;

  fd_ = fd_in;
  curr_buf_ = 0;

  if (!InitKMS())
    return false;
  if (!InitEGL())
    return false;
  if (!InitFramebuffer())
    return false;

  cube_ = new SpinningCube();
  cube_->Init();
  cube_->set_size(mode_.hdisplay, mode_.vdisplay);
  cube_->set_color(1.0, 1.0, 1.0);
  cube_->UpdateForTimeDelta(0);

  is_initialized_ = true;

  return true;
}

void MagmaSpinningCubeApp::Cleanup(){
  is_cleaning_up_ = true;

  // block teardown until outstanding pageflips are serviced
  if (waiting_on_page_flip_) {
    drmEventContext ev;
    memset(&ev, 0, sizeof(ev));
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = modeset_callback;
    fprintf(stderr, "Clearing outstanding page flips\n");
    while (waiting_on_page_flip_) {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      struct timeval timeout;
      memset(&timeout, 0, sizeof(timeout));
      timeout.tv_sec = 1;

      if (select(fd + 1, &fds, nullptr, nullptr, &timeout) < 0) {
        perror("select() failed, giving up on clearing outstanding page flips.");
        break;
      } else if (FD_ISSET(fd, &fds)) {
        if(drmHandleEvent(fd, &ev)){
          fprintf(stderr, "drmHandleEvent failed, giving up on clearing outstanding page flips.\n");
          break;
        }
      }
    }


  }

  delete cube_;
  CleanupFramebuffer();
  CleanupEGL();
  CleanupKMS();
  close(fd_);

  fprintf(stderr, "\nCLeaned Up Successfully, Exiting Cleanly\n\n");
}

MagmaSpinningCubeApp::~MagmaSpinningCubeApp() {
  Cleanup();
}

bool MagmaSpinningCubeApp::InitKMS() {
  resources_ = drmModeGetResources(fd);
  if (!resources_) {
    fprintf(stderr, "drmModeGetResources failed\n");
    return false;
  }

  int i;
  for (i = 0; i < resources_->count_connectors; i++) {
    connector_ = drmModeGetConnector(fd, resources_->connectors[i]);
    if (connector_ == nullptr)
      continue;

    if (connector_->connection == DRM_MODE_CONNECTED &&
        connector_->count_modes > 0)
      break;

    drmModeFreeConnector(connector_);
    connector_ = nullptr;
  }

  if (i == resources_->count_connectors) {
    fprintf(stderr, "No currently active connector found.\n");
    return false;
  }

  for (i = 0; i < resources_->count_encoders; i++) {
    encoder_ = drmModeGetEncoder(fd, resources_->encoders[i]);

    if (encoder_ == nullptr)
      continue;

    if (encoder_->encoder_id == connector_->encoder_id)
      break;

    encoder_ = nullptr;
  }

  if (encoder_ == nullptr) {
    fprintf(stderr, "Could not get encoder for connector_\n");
    return false;
  }

  mode_ = connector_->modes[0];

  saved_crtc_ = drmModeGetCrtc(fd, encoder_->crtc_id);\
  if(!saved_crtc_){
    fprintf(stderr, "Warning: Could not save currently set crtc\n");
  }

  return true;
}

bool MagmaSpinningCubeApp::InitEGL() {
  gbm_ = gbm_create_device(fd);
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
  auto egl_image_target_renderbuffer_storage =
      reinterpret_cast<PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC>(
          eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES"));

  auto egl_image_target_texture_2d =
      reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
          eglGetProcAddress("glEGLImageTargetTexture2DOES"));

  for (int i = 0; i < bufcount_; i++) {
    fb_[i].bo =
        gbm_bo_create(gbm_, mode_.hdisplay, mode_.vdisplay, GBM_BO_FORMAT_ARGB8888,
                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!fb_[i].bo) {
      perror("could not create GBM buffer");
      return false;
    }
    uint32_t stride = gbm_bo_get_stride(fb_[i].bo);

    fb_[i].image = eglCreateImage(display_, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR,
                                 fb_[i].bo, nullptr);
    if (!fb_[i].image) {
      fprintf(stderr, "eglCreateImage returned null\n");
      return false;
    }

    glGenFramebuffers(1, &fb_[i].fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb_[i].fb);

    glGenTextures(1, &fb_[i].color_rb);
    glBindTexture(GL_TEXTURE_2D, fb_[i].color_rb);
    egl_image_target_texture_2d(GL_TEXTURE_2D, fb_[i].image);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           fb_[i].color_rb, 0);

    glGenRenderbuffers(1, &fb_[i].depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, fb_[i].depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, mode_.hdisplay,
                          mode_.vdisplay);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, fb_[i].depth_rb);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      fprintf(stderr, "framebuffer is incomplete\n");
      return false;
    }

    fb_[i].fb_id = 0;
    uint32_t handle = gbm_bo_get_handle(fb_[i].bo).u32;
    if(drmModeAddFB(fd, mode_.hdisplay, mode_.vdisplay, 24, 32, stride, handle, &fb_[i].fb_id)) {
      return false;
    }
  }

  if(drmModeSetCrtc(fd, encoder_->crtc_id, fb_[0].fb_id, 0, 0,
                 &(connector_->connector_id), 1, &mode_)){
    fprintf(stderr, "failed to set crtc\n");
    return false;
  }

  return true;
}

void MagmaSpinningCubeApp::CleanupFramebuffer() {

  for (int i = 0; i < bufcount_; i++) {
    if (fb_[i].fb_id) {
      drmModeRmFB(fd, fb_[i].fb_id);
    }

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

void MagmaSpinningCubeApp::CleanupKMS() {

  if (saved_crtc_) {
    if(drmModeSetCrtc(fd, saved_crtc_->crtc_id, saved_crtc_->buffer_id,
                   saved_crtc_->x, saved_crtc_->y, &(connector_->connector_id), 1,
                   &(saved_crtc_->mode))){
      fprintf(stderr, "Warning: could not restore saved crtc\n");
    }
  }

  if (encoder_) {
    drmModeFreeEncoder(encoder_);
  }

  if (resources_) {
    drmModeFreeResources(resources_);
  }

  if (connector_) {
    drmModeFreeConnector(connector_);
  }
}

bool MagmaSpinningCubeApp::Draw(uint32_t time_delta_ms) {
  waiting_on_page_flip_ = false;
  if (is_cleaning_up_)
    return true;

  glBindFramebuffer(GL_FRAMEBUFFER, fb_[curr_buf_].fb);
  cube_->UpdateForTimeDelta(time_delta_ms / 1000.f);
  cube_->Draw();
  glFinish();

  if (drmModePageFlip(fd, encoder_->crtc_id, fb_[curr_buf_].fb_id,
                      DRM_MODE_PAGE_FLIP_EVENT, this)) {
    printf("Failed to page flip\n");
    encountered_async_error_ = true;
    return false;
  }
  waiting_on_page_flip_ = true;
  curr_buf_ = (curr_buf_ + 1) % bufcount_;

  return true;
}

int main() {

  int fd = open("/dev/dri/card0", O_RDWR);
  if (fd < 0) {
    perror("could not open dri device node");
    return -1;
  }

  MagmaSpinningCubeApp app;
  if (!app.Initialize(fd)) {
    return -1;
  }

  drmEventContext ev;
  memset(&ev, 0, sizeof(ev));
  ev.version = DRM_EVENT_CONTEXT_VERSION;
  ev.page_flip_handler = modeset_callback;

  if (!app.Draw(0)) {
    fprintf(stderr, "draw failed\n");
    return -1;
  }
  while (app.CanDraw()) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fileno(stdin), &fds);
    FD_SET(fd, &fds);
    struct timeval timeout;
    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_sec = 1;

    if (select(fd + 1, &fds, nullptr, nullptr, &timeout) < 0) {
      perror("select() failed");
      break;
    } else if (FD_ISSET(fileno(stdin), &fds)) {
      fprintf(stderr, "exit due to user-input\n");
      break;
    } else if (FD_ISSET(fd, &fds)) {
      drmHandleEvent(fd, &ev);
    }
  }
  return 0;
}