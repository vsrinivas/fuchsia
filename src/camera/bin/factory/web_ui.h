// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_FACTORY_WEB_UI_H_
#define SRC_CAMERA_BIN_FACTORY_WEB_UI_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>

#include "src/camera/bin/factory/capture.h"
#include "src/lib/fsl/tasks/fd_waiter.h"

namespace camera {

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

  WebUI();
  ~WebUI();

  // start listening on a port (thread safe)
  void PostListen(int port);

  // listen for and handle new clients
  void Listen(int port);

 private:

  void ListenWaiter();
  void OnListenReady(zx_status_t success, uint32_t events);
  void HandleClient(FILE* fp);

  // handle capture replies
  void RequestCapture(FILE* fp, WriteFlags flags, bool saveToStorage);

  // controller hooks, owned by the controller (WebUI is a view in MVC)
  WebUIControl* control_;

  async::Loop loop_;
  int listen_sock_;
  fsl::FDWaiter listen_waiter_;
  bool is_bayer_ = false;
  Crop crop_ = { 0, 0, 0, 0 };
  bool is_center_ = false;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_FACTORY_WEB_UI_H_
