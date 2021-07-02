// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_TEST_BASE_H_
#define SRC_DEVICES_LIB_DRIVER2_TEST_BASE_H_

#include <fuchsia/io/cpp/fidl_test_base.h>

#include "src/devices/lib/driver2/namespace.h"

namespace driver::testing {

class Directory : public fuchsia::io::testing::Directory_TestBase {
 public:
  using OpenHandler =
      fit::function<void(std::string path, fidl::InterfaceRequest<fuchsia::io::Node> object)>;

  void SetOpenHandler(OpenHandler open_handler) { open_handler_ = std::move(open_handler); }

 private:
  void Open(uint32_t flags, uint32_t mode, std::string path,
            fidl::InterfaceRequest<fuchsia::io::Node> object) override {
    open_handler_(std::move(path), std::move(object));
  }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: Directory::%s\n", name.data());
  }

  OpenHandler open_handler_;
};

zx::status<Namespace> CreateNamespace(fidl::ClientEnd<fuchsia_io::Directory> client_end);

}  // namespace driver::testing

#endif  // SRC_DEVICES_LIB_DRIVER2_TEST_BASE_H_
