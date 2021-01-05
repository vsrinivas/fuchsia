// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/protocoleventadd/llcpp/fidl.h>  // nogncheck
namespace fidl_test = llcpp::fidl::test::protocoleventadd;

// [START contents]
class AsyncEventHandler : public fidl_test::Example::AsyncEventHandler {
  void OnExistingEvent(fidl_test::Example::OnExistingEventResponse* event) override {}
  void OnNewEvent(fidl_test::Example::OnNewEventResponse* event) override {}
};

class SyncEventHandler : public fidl_test::Example::SyncEventHandler {
  void OnExistingEvent(fidl_test::Example::OnExistingEventResponse* event) override {}
  void OnNewEvent(fidl_test::Example::OnNewEventResponse* event) override {}
};

void sendEvents(fidl::ServerBindingRef<fidl_test::Example> server) {
  server->OnExistingEvent();
  server->OnNewEvent();
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
