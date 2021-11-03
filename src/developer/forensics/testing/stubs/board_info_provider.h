// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_BOARD_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_BOARD_INFO_PROVIDER_H_

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl_test_base.h>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using BoardInfoProviderBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::hwinfo, Board);

class BoardInfoProvider : public BoardInfoProviderBase {
 public:
  BoardInfoProvider(fuchsia::hwinfo::BoardInfo&& info) : info_(std::move(info)) {}

  // |fuchsia::hwinfo::Board|
  void GetInfo(GetInfoCallback callback) override;

 private:
  fuchsia::hwinfo::BoardInfo info_;
  bool has_been_called_ = false;
};

class BoardInfoProviderNeverReturns : public BoardInfoProviderBase {
 public:
  // |fuchsia::hwinfo::Board|
  STUB_METHOD_DOES_NOT_RETURN(GetInfo, GetInfoCallback)
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_BOARD_INFO_PROVIDER_H_
