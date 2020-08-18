// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_FACTORY_WEB_UI_H_
#define SRC_CAMERA_BIN_FACTORY_WEB_UI_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>

#include "src/camera/bin/factory/capture.h"
#include "src/lib/fsl/tasks/fd_waiter.h"

namespace camera {

enum RGBConversionType : uint8_t {
  NONE,    // treat frame as 8-bit grayscale, do not process to RGB
  NATIVE,  // use pixel_format to select RGB conversion method
  NV12,    // treat frame as NV12, do YUV to RGB conversion
  BAYER,   // treat frame as 8-bit raw sensor data in NV12 stream (output Y plane)
};

// an interface for caller to provide access to resources
class WebUIControl {
 public:
  virtual void RequestCaptureData(uint32_t stream_index, CaptureResponse callback) = 0;
};

// A Web UI for controlling camera-gym from the host
class WebUI {
 public:
  // create a new WebUI.
  static fit::result<std::unique_ptr<WebUI>, zx_status_t> Create(WebUIControl* control);

  ~WebUI();

  // start listening on a port
  void PostListen(int port);

 private:
  WebUI();

  // listen for and handle new clients
  void Listen(int port);
  void ListenWaiter();
  void OnListenReady(zx_status_t success, uint32_t events);
  void HandleClient(FILE* fp);

  // handle capture replies
  void RequestCapture(FILE* fp, RGBConversionType convert, bool saveToStorage);

  // controller hooks, owned by the controller (WebUI is a view in MVC)
  WebUIControl* control_;

  async::Loop loop_;
  int listen_sock_;
  fsl::FDWaiter listen_waiter_;
  bool isBayer_ = false;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_FACTORY_WEB_UI_H_
