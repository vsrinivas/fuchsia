// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_layer.h"

namespace btlib {
namespace gatt {
namespace testing {

void FakeLayer::Initialize() {
  // TODO: implement
}

void FakeLayer::ShutDown() {
  // TODO: implement
}

void FakeLayer::AddConnection(const std::string& peer_id,
                              fbl::RefPtr<l2cap::Channel> att_chan) {
  // TODO: implement
}

void FakeLayer::RemoveConnection(std::string peer_id) {
  // TODO: implement
}

void FakeLayer::RegisterService(ServicePtr service,
                                ServiceIdCallback callback,
                                ReadHandler read_handler,
                                WriteHandler write_handler,
                                ClientConfigCallback ccc_callback,
                                fxl::RefPtr<fxl::TaskRunner> task_runner) {
  // TODO: implement
}

void FakeLayer::UnregisterService(IdType service_id) {
  // TODO: implement
}

void FakeLayer::SendNotification(IdType service_id,
                                 IdType chrc_id,
                                 std::string peer_id,
                                 ::f1dl::Array<uint8_t> value,
                                 bool indicate) {
  // TODO: implement
}

}  // namespace testing
}  // namespace gatt
}  // namespace btlib
