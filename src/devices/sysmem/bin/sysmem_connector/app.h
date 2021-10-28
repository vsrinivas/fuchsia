// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_BIN_SYSMEM_CONNECTOR_APP_H_
#define SRC_DEVICES_SYSMEM_BIN_SYSMEM_CONNECTOR_APP_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sysmem-connector/sysmem-connector.h>

#include <memory>

#include "src/lib/fxl/macros.h"

// sysmem_connector's app
class App {
 public:
  explicit App(async_dispatcher_t* dispatcher);
  ~App();

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::unique_ptr<sys::ComponentContext> component_context_;

  sysmem_connector_t* sysmem_connector_ = nullptr;

  sys::OutgoingDirectory outgoing_aux_service_directory_parent_;
  vfs::PseudoDir* outgoing_aux_service_directory_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

#endif  // SRC_DEVICES_SYSMEM_BIN_SYSMEM_CONNECTOR_APP_H_
