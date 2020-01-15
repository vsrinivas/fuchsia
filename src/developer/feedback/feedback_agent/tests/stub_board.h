// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_BOARD_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_BOARD_H_

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>

#include <string>

namespace feedback {

// Stub Board service to return controlled response to Board::GetInfo().
class StubBoard : public fuchsia::hwinfo::Board {
 public:
  StubBoard(fuchsia::hwinfo::BoardInfo&& info) : info_(std::move(info)) {}

  // Returns a request handler for a binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::hwinfo::Board> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::hwinfo::Board> request) {
      binding_ = std::make_unique<fidl::Binding<fuchsia::hwinfo::Board>>(this, std::move(request));
    };
  }

  // |fuchsia.hwinfo.Board|
  void GetInfo(GetInfoCallback callback) override;

 protected:
  void CloseConnection();

 private:
  std::unique_ptr<fidl::Binding<fuchsia::hwinfo::Board>> binding_;
  fuchsia::hwinfo::BoardInfo info_;
  bool has_been_called_ = false;
};

class StubBoardNeverReturns : public StubBoard {
 public:
  StubBoardNeverReturns() : StubBoard(fuchsia::hwinfo::BoardInfo()) {}

  // |fuchsia.hwinfo.Board|
  void GetInfo(GetInfoCallback callback) override;
};
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_BOARD_H_
