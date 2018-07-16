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
                                ClientConfigCallback ccc_callback) {
  // TODO: implement
}

void FakeLayer::UnregisterService(IdType service_id) {
  // TODO: implement
}

void FakeLayer::SendNotification(IdType service_id,
                                 IdType chrc_id,
                                 std::string peer_id,
                                 ::fidl::VectorPtr<uint8_t> value,
                                 bool indicate) {
  // TODO: implement
}

void FakeLayer::DiscoverServices(std::string peer_id) {
  // TODO: implement
}

void FakeLayer::RegisterRemoteServiceWatcher(RemoteServiceWatcher callback,
                                             async_dispatcher_t* dispatcher) {
  // TODO: implement
}

void FakeLayer::ListServices(std::string peer_id,
                             std::vector<common::UUID> uuids,
                             ServiceListCallback callback) {
  // TODO: implement
}

void FakeLayer::FindService(std::string peer_id,
                            IdType service_id,
                            RemoteServiceCallback callback) {
  // TODO: implement
}

}  // namespace testing
}  // namespace gatt
}  // namespace btlib
