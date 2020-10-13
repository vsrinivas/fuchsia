// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/factory/web_ui.h"

#include <arpa/inet.h>
#include <errno.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <zircon/types.h>

#include <memory>

namespace camera {

fit::result<std::unique_ptr<WebUI>, zx_status_t> WebUI::Create(WebUIControl* control) {
  auto webui = std::make_unique<WebUI>();
  webui->control_ = control;
  zx_status_t status = webui->loop_.StartThread("WebUI Thread");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  return fit::ok(std::move(webui));
}

WebUI::WebUI() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), listen_sock_(-1) {}

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
    .sin6_family = AF_INET6, .sin6_port = htons(static_cast<uint16_t>(port)),
    .sin6_addr = in6addr_any,
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
  if (fd < 0) {
    FX_LOGS(ERROR) << "accept failed: " << strerror(errno);
    return;
  }
  FX_LOGS(INFO) << "accepted connection from client";
  FILE* fp = fdopen(fd, "a+");
  if (fp == NULL) {
    FX_LOGS(ERROR) << "fdopen failed: " << strerror(errno);
    return;
  }
  setlinebuf(fp);
  fd_set r;
  FD_ZERO(&r);
  FD_SET(fd, &r);
  struct timeval to = {0, 200000};
  if (select(fd + 1, &r, NULL, NULL, &to) != 1) {
    FX_LOGS(INFO) << "hanging up on slow client";
  } else {
    HandleClient(fp);
  }
  fclose(fp);
  ListenWaiter();
}

void WebUI::HandleClient(FILE* fp) {
  char buf[1024] = {0};
  if (fgets(buf, sizeof(buf), fp) == NULL) {
    FX_LOGS(ERROR) << "fgets failed: " << strerror(errno);
    return;
  }
  char get[] = "GET /";
  if (strncmp(buf, get, sizeof(get) - 1) != 0) {
    FX_LOGS(ERROR) << "expected '" << get << "', got '" << buf << "'";
    return;
  }
  char* cmd = buf + sizeof(get) - 2;
  char* space = index(buf + sizeof(get) - 1, ' ');
  if (space == NULL) {
    FX_LOGS(ERROR) << "expected GET /... ";
    return;
  }
  *space = 0;
  FX_LOGS(INFO) << "processing request: " << cmd;
  if (strcmp(cmd, "/") == 0 || strcmp(cmd, "/index.html") == 0) {
    fputs("HTTP/1.1 200 OK\nContent-Type: text/html\n\n", fp);
    fputs(R"HTML(<html><body>
                 <a href="frame">frame</a> - download a frame
                                             (pgm or raw format depending on bayer mode)<br>
                 <a href="save">save</a> - save a frame to /data/capture.pgm or .raw<br>
                 <a href="bayer/on">bayer/on</a> - output raw sensor bayer image<br>
                 <a href="bayer/off">bayer/off</a> - output normal processed image<br>
                 <a href="crop/upper-left">crop/upper-left</a> - crop to top left corner<br>
                 <a href="crop/lower-right">crop/lower-right</a> - crop to lower right corner<br>
                 <a href="crop/center">crop/center</a> - crop to center<br>
                 <a href="crop/off">crop/off</a> - crop to top left corner<br>
                 <br>
                )HTML",
          fp);
    fprintf(fp, "Bayer mode is %s.<br>\n", is_bayer_ ? "ON" : "OFF");
    fprintf(fp, "Crop is %d %d %d %d, CENTER is %s.<br>\n", crop_.x, crop_.y, crop_.width,
            crop_.height, is_center_ ? "ON" : "OFF");
    fputs(R"HTML(<br>
                 The follow may be useful for debugging:<br>
                 <a href="frame/png">frame/png</a> - same as /frame but as png<br>
                 <a href="frame/nv12">frame/nv12</a> - process as NV12<br>
                 <a href="frame/bayer8">frame/bayer8</a> - process as raw sensor data, 8-bit<br>
                 <a href="frame/bayer16">frame/bayer16</a> - process as raw sensor data, 16-bit<br>
                 <a href="frame/unprocessed">frame/unprocessed</a> - frame bits as 8-bit gray<br>
                )HTML",
          fp);
    return;
  }

  auto bayer_flag = is_bayer_ ? WriteFlags::MOD_BAYER8HACK : WriteFlags::NONE;

  // expected factory usage
  if (strcmp(cmd, "/frame") == 0) {
    auto out = is_bayer_ ? WriteFlags::OUT_RAW : WriteFlags::OUT_PGM;
    RequestCapture(fp, WriteFlags::IN_DEFAULT | out | bayer_flag, false);
    return;
  }
  if (strcmp(cmd, "/save") == 0) {
    auto out = is_bayer_ ? WriteFlags::OUT_RAW : WriteFlags::OUT_PGM;
    RequestCapture(fp, WriteFlags::IN_DEFAULT | out | bayer_flag, true);
    return;
  }
  if (strcmp(cmd, "/bayer/on") == 0) {
    is_bayer_ = true;
    fputs("HTTP/1.1 200 OK\nContent-Type: text/html\n\nbayer mode is on\n", fp);
    return;
  }
  if (strcmp(cmd, "/bayer/off") == 0) {
    is_bayer_ = false;
    fputs("HTTP/1.1 200 OK\nContent-Type: text/html\n\nbayer mode is off\n", fp);
    return;
  }
  if (strcmp(cmd, "/crop/upper-left") == 0) {
    crop_ = {0, 0, 500, 500};
    is_center_ = false;
    fputs("HTTP/1.1 200 OK\nContent-Type: text/html\n\ncrop is upper-left\n", fp);
    return;
  }
  if (strcmp(cmd, "/crop/lower-right") == 0) {
    crop_ = {1500, 1500, 500, 500};
    is_center_ = false;
    fputs("HTTP/1.1 200 OK\nContent-Type: text/html\n\ncrop is lower-right\n", fp);
    return;
  }
  if (strcmp(cmd, "/crop/center") == 0) {
    crop_ = {0, 0, 500, 500};
    is_center_ = true;
    fputs("HTTP/1.1 200 OK\nContent-Type: text/html\n\ncrop is center\n", fp);
    return;
  }
  if (strcmp(cmd, "/crop/off") == 0) {
    crop_ = {0, 0, 0, 0};
    is_center_ = false;
    fputs("HTTP/1.1 200 OK\nContent-Type: text/html\n\ncrop is off\n", fp);
    return;
  }

  // for debugging (png shows in chrome)
  if (strcmp(cmd, "/frame/png") == 0) {
    RequestCapture(fp, WriteFlags::IN_DEFAULT | WriteFlags::OUT_PNG_GRAY | bayer_flag, false);
    return;
  }
  if (strcmp(cmd, "/frame/nv12") == 0) {
    RequestCapture(fp, WriteFlags::IN_NV12 | WriteFlags::OUT_PNG_RGB, false);
    return;
  }
  if (strcmp(cmd, "/frame/bayer8") == 0) {
    RequestCapture(
        fp, WriteFlags::IN_BAYER8 | WriteFlags::OUT_PNG_GRAY | WriteFlags::MOD_BAYER8HACK, false);
    return;
  }
  if (strcmp(cmd, "/frame/bayer16") == 0) {
    RequestCapture(fp, WriteFlags::IN_BAYER16 | WriteFlags::OUT_PNG_GRAY, false);
    return;
  }
  if (strcmp(cmd, "/frame/unprocessed") == 0) {
    RequestCapture(
        fp, WriteFlags::IN_DEFAULT | WriteFlags::OUT_PNG_GRAY | WriteFlags::MOD_UNPROCESSED, false);
    return;
  }

  fputs("HTTP/1.1 404 Not Found\n", fp);
}

void WebUI::RequestCapture(FILE* fp, WriteFlags flags, bool saveToStorage) {
  FILE* fp2 = fdopen(dup(fileno(fp)), "w");
  if (fp2 == NULL) {
    FX_LOGS(ERROR) << "failed to fdopen/dup: " << strerror(errno);
    fputs("HTTP/1.1 500 Internal Server Error\n\nERROR: dup failed", fp2);
    return;
  }
  control_->RequestCaptureData(0, [this, fp2, flags, saveToStorage](
                                      zx_status_t status, std::unique_ptr<Capture> frame) {
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "RequestCaptureData failed";
      fputs("HTTP/1.1 500 Internal Server Error\n\nERROR: capture failed", fp2);
      fclose(fp2);
      return;
    }
    FILE* filefp = fp2;  // default to write png in HTTP reply
    std::string file = is_bayer_ ? "capture.raw" : "capture.pgm";
    std::string path = "/data/" + file;
    if (saveToStorage) {
      filefp = fopen(path.c_str(), "w");
      if (filefp == NULL) {
        FX_LOGS(ERROR) << "failed to open " << path << ": " << strerror(errno);
        fputs("HTTP/1.1 500 Internal Server Error\n\nERROR: local file write failed", fp2);
        fclose(fp2);
        return;
      }
    }

    const char* mime = saveToStorage                            ? "text/html"
                       : (flags & kPNGMask) != WriteFlags::NONE ? "image/png"
                       : (flags & kPNMMask) != WriteFlags::NONE ? "image/x-portable-anymap"
                                                                : "application/octet-stream";

    fprintf(fp2, "HTTP/1.1 200 OK\nContent-Type: %s\n", mime);
    fprintf(fp2, "Content-Disposition: filename=\"%s\"\n\n", file.c_str());

    if (saveToStorage) {
      fputs("Please wait while frame is written to local storage...<br>\n", fp2);
      fflush(fp2);
    }

    auto center = is_center_ ? WriteFlags::MOD_CENTER : WriteFlags::NONE;
    auto crop = crop_;
    status = frame->WriteImage(filefp, flags | center, crop);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status);
      if (filefp != fp2) {
        fclose(filefp);
      }
      fclose(fp2);
      return;
    }
    FX_LOGS(INFO) << "crop: " << crop.x << "," << crop.y << "," << crop.width << "," << crop.height;
    if (filefp != fp2) {
      fputs("Frame saved to local storage.<br>", fp2);
      std::string real = "/data/r/sys/fuchsia.com:camera-factory:0#meta:camera-factory.cmx/" + file;
      fprintf(fp2, "Wrote %s, which is likely %s<br>", path.c_str(), real.c_str());
      fclose(filefp);
    }
    fclose(fp2);
  });
}

}  // namespace camera
