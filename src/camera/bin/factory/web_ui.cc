// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/factory/web_ui.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <poll.h>

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>
#include <zircon/types.h>

#include <png.h>

namespace camera {

fit::result<std::unique_ptr<WebUI>, zx_status_t> WebUI::Create(WebUIControl* control) {
  auto webui = std::unique_ptr<WebUI>(new WebUI());
  webui->control_ = control;
  zx_status_t status = webui->loop_.StartThread("WebUI Thread");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  return fit::ok(std::move(webui));
}

WebUI::WebUI() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), listen_sock_(-1) {
}

WebUI::~WebUI() {
  loop_.Quit();
  loop_.JoinThreads();
}

void WebUI::PostListen(int port) {
  async::PostTask(loop_.dispatcher(), [this, port]() mutable { Listen(port); });
}

void WebUI::Listen(int port) {
  listen_sock_ = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (listen_sock_ < 0) {
    FX_LOGS(WARNING) << "socket(AF_INET6, SOCK_STREAM) failed: " << strerror(errno);
    close(listen_sock_);
    return;
  }
  const struct sockaddr_in6 saddr {
    .sin6_family = AF_INET6, .sin6_port = htons(port), .sin6_addr = in6addr_any,
  };
  int enable = 1;
  if (setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof enable) < 0) {
    FX_LOGS(ERROR) << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno);
    close(listen_sock_);
    return;
  }
  if (bind(listen_sock_, reinterpret_cast<const sockaddr*>(&saddr), sizeof saddr) < 0) {
    FX_LOGS(ERROR) << "bind failed: " << strerror(errno);
    close(listen_sock_);
    return;
  }
  FX_LOGS(INFO) << "WebUI::Listen: listening on port " << port;
  if (listen(listen_sock_, 5) < 0) {
    FX_LOGS(ERROR) << "listen failed: " << strerror(errno);
    close(listen_sock_);
    return;
  }
  ListenWaiter();
}

void WebUI::ListenWaiter() {
  listen_waiter_.Wait(
      [this](zx_status_t success, uint32_t events) { OnListenReady(success, events); },
      listen_sock_, POLLIN);
}

void WebUI::OnListenReady(zx_status_t success, uint32_t events) {
  struct sockaddr_in6 peer {};
  socklen_t peer_len = sizeof(peer);
  int fd = accept(listen_sock_, reinterpret_cast<struct sockaddr*>(&peer), &peer_len);
  ZX_ASSERT(fd >= 0);
  HandleClient(fd);
  close(fd);
  ListenWaiter();
}

void WebUI::HandleClient(int fd) {
  char buf[1024];
  size_t len = read(fd, buf, sizeof(buf) - 1);
  buf[len] = 0;
  char get[] = "GET /";
  if (strncmp(buf, get, sizeof(get) - 1) != 0) {
    FX_LOGF(INFO, "", "WebUI: expected '%s', got '%s'", get, buf);
    return;
  }
  char *cmd = buf + sizeof(get) - 1;
  char* space = index(buf + sizeof(get) - 1, ' ');
  if (space == NULL) {
    FX_LOGF(INFO, "", "WebUI: %s", "expected GET /... ");
    return;
  }
  *space = 0;
  FX_LOGF(INFO, "", "WebUI: processing request: '%s'", cmd);
  if (strcmp(cmd, "") == 0 || strcmp(cmd, "index.html") == 0) {
    char reply[] = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n<html><body>It Worked!\n";
    write(fd, reply, sizeof(reply) - 1);
    return;
  }
  if (strcmp(cmd, "info") == 0) {
    char reply[] = "HTTP/1.1 200 OK\nContent-Type: text/html\n\nhere is some info\n";
    write(fd, reply, sizeof(reply) - 1);
    return;
  }
  if (strcmp(cmd, "frame") == 0) {
    int newfd = dup(fd);
    if (newfd < 0) {
      FX_LOGS(ERROR) << "failed to dup fd: " << strerror(errno);
      return;
    }
    control_->RequestCaptureData(0, [this, newfd](zx_status_t status,
                                                  std::unique_ptr<Capture> frame) {
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "RequestCaptureData failed";
        close(newfd);
        return;
      }
      char mime[] = "image/png";
      uint32_t vmo_size = frame->image_->size();
      char buf[1024];
      sprintf(buf, "HTTP/1.1 200 OK\nContent-Type: %s\nXContent-Length: %u\n\n", mime, vmo_size);
      write(newfd, buf, strlen(buf));

      auto& iformat = frame->properties_.image_format;
      auto& pformat = iformat.pixel_format;
      switch (pformat.type) {
      case fuchsia::sysmem::PixelFormatType::NV12:
        WritePNGFromNV12(newfd, std::move(frame));
        break;
      default:
        WritePNGFromRaw(newfd, std::move(frame));
        break;
      }
      close(newfd);
    });
    return;
  }
  if (strcmp(cmd, "raw") == 0) {
    int newfd = dup(fd);
    if (newfd < 0) {
      FX_LOGS(ERROR) << "failed to dup fd";
      return;
    }
    control_->RequestCaptureData(0, [this, newfd](zx_status_t status,
                                                  std::unique_ptr<Capture> frame) {
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "RequestCaptureData failed";
        close(newfd);
        return;
      }
      char mime[] = "image/png";
      uint32_t vmo_size = frame->image_->size();
      char buf[1024];
      sprintf(buf, "HTTP/1.1 200 OK\nContent-Type: %s\nXContent-Length: %u\n\n", mime, vmo_size);
      write(newfd, buf, strlen(buf));

      WritePNGFromRaw(newfd, std::move(frame));
      close(newfd);
    });
    return;
  }
  if (strcmp(cmd, "nv12") == 0) {
    int newfd = dup(fd);
    if (newfd < 0) {
      FX_LOGS(ERROR) << "failed to dup fd";
      return;
    }
    control_->RequestCaptureData(0, [this, newfd](zx_status_t status,
                                                  std::unique_ptr<Capture> frame) {
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "RequestCaptureData failed";
        close(newfd);
        return;
      }
      char mime[] = "image/png";
      uint32_t vmo_size = frame->image_->size();
      char buf[1024];
      sprintf(buf, "HTTP/1.1 200 OK\nContent-Type: %s\nXContent-Length: %u\n\n", mime, vmo_size);
      write(newfd, buf, strlen(buf));

      WritePNGFromNV12(newfd, std::move(frame));
      close(newfd);
    });
    return;
  }
  if (strcmp(cmd, "save") == 0) {
    int newfd = dup(fd);
    if (newfd < 0) {
      FX_LOGS(ERROR) << "failed to dup fd";
      return;
    }
    control_->RequestCaptureData(0, [this, newfd](zx_status_t status,
                                                  std::unique_ptr<Capture> frame) {
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "RequestCaptureData failed";
        close(newfd);
        return;
      }
      char mime[] = "text/html";
      uint32_t vmo_size = frame->image_->size();
      char buf[1024];
      sprintf(buf, "HTTP/1.1 200 OK\nContent-Type: %s\nXContent-Length: %u\n\n", mime, vmo_size);
      write(newfd, buf, strlen(buf));

      char cwd[1024];
      *cwd = 0;
      if (getcwd(cwd, sizeof cwd) == NULL) {
        FX_LOGS(WARNING) << "getcwd failed: " << strerror(errno);
      }
      FX_LOGS(WARNING) << "cwd=" << cwd;
      char file[] = "/tmp/capture.png";
      int file_fd = open(file, O_WRONLY|O_CREAT, 0666);
      if (file_fd < 0) {
        FX_LOGS(ERROR) << "failed to open " << file << ": " << strerror(errno);
        close(newfd);
        return;
      }
      WritePNGFromNV12(file_fd, std::move(frame));
      close(file_fd);
      close(newfd);
    });
    return;
  }
  FX_LOGF(INFO, "", "WebUI: Not Found: %s", cmd);
  char reply[] = "HTTP/1.1 404 Not Found\n";
  write(fd, reply, sizeof(reply) - 1);
}

void WebUI::WritePNGFromNV12(int newfd, std::unique_ptr<Capture> frame) {
  FILE* f = fdopen(newfd, "w");         // do not fclose(), caller will close(newfd);
  if (f == NULL) {
    FX_LOGS(ERROR) << "fdopen failed: " << strerror(errno);
    return;
  }

  auto& iformat = frame->properties_.image_format;
  auto& pformat = iformat.pixel_format;
  FX_LOGS(INFO) << "writing format " << int(pformat.type) << " as NV12";

  // NV12 is 8 bit Y (width x height) then 8 bit UV (width x height/2)

  uint32_t width = iformat.coded_width;
  uint32_t height = iformat.coded_height;
  auto ypos = frame->image_->data();
  auto uvpos = ypos + iformat.bytes_per_row * height;

  auto png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  auto info_ptr = png_create_info_struct(png_ptr);
  if (setjmp(png_jmpbuf(png_ptr))) {
    FX_LOGS(ERROR) << "libpng failed";
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return;
  }
  png_init_io(png_ptr, f);
  png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  png_write_info(png_ptr, info_ptr);

  for (uint32_t i = 0; i < height; i++) {
    png_byte row[width * 3];
    for (uint32_t j = 0; j < width; j++) {
      int32_t y = ypos[j];
      int32_t u = uvpos[j/2+1] - 128;
      int32_t v = uvpos[j/2] - 128;
      // RGB
#define CLIP(x) ((x) < 0 ? 0 : (x) > 255 ? 255 : (x))
      row[j*3+0] = CLIP(y  + 1.402 * v);
      row[j*3+1] = CLIP(y - 0.34414 * u - 0.71414 * v);
      row[j*3+2] = CLIP(y + 1.772 * u);
    }
    png_write_row(png_ptr, row);
    ypos += iformat.bytes_per_row;
    if (i % 2) {
      uvpos += iformat.bytes_per_row;
    }
  }
  png_write_end(png_ptr, NULL);
  png_destroy_write_struct(&png_ptr, &info_ptr);

  fflush(f);
  // do not close f, caller will close newfd
  return;
}

// write image as grayscale png
void WebUI::WritePNGFromRaw(int newfd, std::unique_ptr<Capture> frame) {
  FILE* f = fdopen(newfd, "w");         // do not fclose(), caller will close(newfd);
  if (f == NULL) {
    FX_LOGS(ERROR) << "fdopen failed";
    return;
  }

  auto& iformat = frame->properties_.image_format;
  auto& pformat = iformat.pixel_format;
  FX_LOGS(INFO) << "writing format " << int(pformat.type) << " as raw grayscale";

  uint32_t vmo_size = frame->image_->size();
  uint32_t width = iformat.bytes_per_row;               // pretend it's 8-bit gray
  uint32_t height = vmo_size / iformat.bytes_per_row;   // number of whole rows, could be non-image

  auto png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  auto info_ptr = png_create_info_struct(png_ptr);
  uint8_t* pos = frame->image_->data();
  if (setjmp(png_jmpbuf(png_ptr))) {
    FX_LOGS(ERROR) << "libpng failed";
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return;
  }
  png_init_io(png_ptr, f);
  png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  png_write_info(png_ptr, info_ptr);

  for (uint32_t i = 0; i < height; i++) {
    png_write_row(png_ptr, pos);                // consumes width bytes
    pos += iformat.bytes_per_row;
  }
  png_write_end(png_ptr, NULL);
  png_destroy_write_struct(&png_ptr, &info_ptr);

  fflush(f);
  // do not close f, caller will close newfd
  return;
}

}  // namespace camera
