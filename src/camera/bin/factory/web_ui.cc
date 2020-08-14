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
#include <png.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <zircon/types.h>

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
  if (fd < 0) {
    FX_LOGS(ERROR) << "accept failed: " << strerror(errno);
    return;
  }
  FX_LOGS(INFO) << "accepted connection from client";
  FILE* fp = fdopen(fd, "r+");
  if (fp == NULL) {
    FX_LOGS(ERROR) << "fdopen failed: " << strerror(errno);
    return;
  }
  setlinebuf(fp);
  HandleClient(fp);
  fclose(fp);
  ListenWaiter();
}

void WebUI::HandleClient(FILE* fp) {
  char buf[1024] = {0};
  if (fgets(buf, sizeof(buf), fp) == NULL) {
    FX_LOGS(ERROR) << "fgets failed";
    return;
  }
  char get[] = "GET /";
  if (strncmp(buf, get, sizeof(get) - 1) != 0) {
    FX_LOGS(ERROR) << "expected '" << get << "', got '" << buf << "'";
    return;
  }
  char* cmd = buf + sizeof(get) - 1;
  char* space = index(buf + sizeof(get) - 1, ' ');
  if (space == NULL) {
    FX_LOGS(ERROR) << "expected GET /... ";
    return;
  }
  *space = 0;
  FX_LOGS(INFO) << "processing request: " << cmd;
  if (strcmp(cmd, "") == 0 || strcmp(cmd, "index.html") == 0) {
    fputs("HTTP/1.1 200 OK\nContent-Type: text/html\n\n", fp);
    fputs(R"HTML(<html><body>
		 <a href="frame">frame</a> - capture and show a frame<br>
                 <a href="save">save</a> - capture a frame, save to /data/capture.png<br>
                 <a href="bayer/on">bayer/on</a> - output raw sensor bayer image<br>
                 <a href="bayer/off">bayer/off</a> - output normal processed image<br>
                 <br>
		)HTML",
          fp);
    fprintf(fp, "Bayer mode is %s.<br>\n", isBayer_ ? "ON" : "OFF");
    fputs(R"HTML(<br>
                 The follow may be useful for debugging:<br>
                 <a href="frame/nv12">frame/nv12</a> - process as NV12<br>
                 <a href="frame/bayer">frame/bayer</a> - process as raw sensor data<br>
                 <a href="frame/unprocessed">frame/unprocessed</a> - do no processing<br>
		)HTML",
          fp);
    return;
  }

  // expected factory usage
  if (strcmp(cmd, "frame") == 0) {
    RequestCapture(fp, NATIVE, false);
    return;
  }
  if (strcmp(cmd, "save") == 0) {
    RequestCapture(fp, NATIVE, true);
    return;
  }
  if (strcmp(cmd, "bayer/on") == 0) {
    control_->SetIspBypassMode(true);
    isBayer_ = true;
    fputs("HTTP/1.1 200 OK\nContent-Type: text/html\n\nbayer mode is on\n", fp);
    return;
  }
  if (strcmp(cmd, "bayer/off") == 0) {
    control_->SetIspBypassMode(false);
    isBayer_ = false;
    fputs("HTTP/1.1 200 OK\nContent-Type: text/html\n\nbayer mode is off\n", fp);
    return;
  }

  // for debugging
  if (strcmp(cmd, "frame/nv12") == 0) {
    RequestCapture(fp, NV12, false);
    return;
  }
  if (strcmp(cmd, "frame/bayer") == 0) {
    RequestCapture(fp, BAYER, false);
    return;
  }
  if (strcmp(cmd, "frame/unprocessed") == 0) {
    RequestCapture(fp, NONE, false);
    return;
  }

  fputs("HTTP/1.1 404 Not Found\n", fp);
}

void WebUI::RequestCapture(FILE* fp, RGBConversionType convert, bool saveToStorage) {
  FILE* fp2 = fdopen(dup(fileno(fp)), "w");
  if (fp2 == NULL) {
    FX_LOGS(ERROR) << "failed to fdopen/dup: " << strerror(errno);
    fputs("HTTP/1.1 500 Internal Server Error\n\nERROR: dup failed", fp2);
    return;
  }
  control_->RequestCaptureData(0, [fp2, isBayer = isBayer_, convert, saveToStorage](
                                      zx_status_t status, std::unique_ptr<Capture> frame) {
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "RequestCaptureData failed";
      fputs("HTTP/1.1 500 Internal Server Error\n\nERROR: capture failed", fp2);
      fclose(fp2);
      return;
    }
    FILE* filefp = fp2;  // default to write png in HTTP reply
    if (saveToStorage) {
      char file[] = "/data/capture.png";
      filefp = fopen(file, "w");
      if (filefp == NULL) {
        FX_LOGS(ERROR) << "failed to open " << file << ": " << strerror(errno);
        fputs("HTTP/1.1 500 Internal Server Error\n\nERROR: local file write failed", fp2);
        fclose(fp2);
        return;
      }
    }

    uint32_t vmo_size = frame->image_->size();
    fprintf(fp2, "HTTP/1.1 200 OK\nContent-Type: %s\nXContent-Length: %u\n\n",
            saveToStorage ? "text/html" : "image/png", vmo_size);

    if (saveToStorage) {
      fputs("Please wait while frame is written to local storage...<br>\n", fp2);
      fflush(fp2);
    }

    auto& iformat = frame->properties_.image_format;
    auto& pformat = iformat.pixel_format;

    switch (convert) {
      default:
        FX_LOGS(INFO) << "unknown CaptureType";
        // fall through to NATIVE
      case NATIVE:
        if (isBayer) {
          frame->WritePNGUnprocessed(filefp, isBayer);
        } else if (pformat.type == fuchsia::sysmem::PixelFormatType::NV12) {
          frame->WritePNGAsNV12(filefp);
        } else {
          FX_LOGS(INFO) << "writing unusual format " << (int)pformat.type << " as unprocessed";
          frame->WritePNGUnprocessed(filefp, false);
        }
        break;
      case NONE:
        frame->WritePNGUnprocessed(filefp, false);
        break;
      case BAYER:
        frame->WritePNGUnprocessed(filefp, true);
        break;
      case NV12:
        frame->WritePNGAsNV12(filefp);
        break;
    }
    if (filefp != fp2) {
      fputs("Frame saved to local storage.<br>", fp2);
      fprintf(fp2, "Path is likely: %s.<br>",
              "/data/r/sys/fuchsia.com:camera-factory:0#meta:camera-factory.cmx/capture.png");
      fclose(filefp);
    }
    fclose(fp2);
  });
}

}  // namespace camera
