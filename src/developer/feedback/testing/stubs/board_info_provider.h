// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_BOARD_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_BOARD_INFO_PROVIDER_H_

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>

#include "src/lib/fxl/logging.h"

namespace feedback {
namespace stubs {

class BoardInfoProvider : public fuchsia::hwinfo::testing::Board_TestBase {
 public:
  BoardInfoProvider(fuchsia::hwinfo::BoardInfo&& info) : info_(std::move(info)) {}

  fidl::InterfaceRequestHandler<fuchsia::hwinfo::Board> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::hwinfo::Board> request) {
      binding_ = std::make_unique<fidl::Binding<fuchsia::hwinfo::Board>>(this, std::move(request));
    };
  }

  void CloseConnection();

  // |fuchsia::hwinfo::Board|
  void GetInfo(GetInfoCallback callback) override;

  // |fuchsia::hwinfo::testing::Board_TestBase|
  void NotImplemented_(const std::string& name) override {
    FXL_NOTIMPLEMENTED() << name << " is not implemented";
  }

 private:
  std::unique_ptr<fidl::Binding<fuchsia::hwinfo::Board>> binding_;
  fuchsia::hwinfo::BoardInfo info_;
  bool has_been_called_ = false;
};

class BoardInfoProviderNeverReturns : public BoardInfoProvider {
 public:
  BoardInfoProviderNeverReturns() : BoardInfoProvider(fuchsia::hwinfo::BoardInfo()) {}

  // |fuchsia::hwinfo::Board|
  void GetInfo(GetInfoCallback callback) override;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_BOARD_INFO_PROVIDER_H_
