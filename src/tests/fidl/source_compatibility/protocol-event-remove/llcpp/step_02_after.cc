// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.protocoleventremove/cpp/wire.h>  // nogncheck
namespace fidl_test = fidl_test_protocoleventremove;

// [START contents]
class AsyncEventHandler : public fidl::WireAsyncEventHandler<fidl_test::Example> {
  void OnExistingEvent(fidl::WireResponse<fidl_test::Example::OnExistingEvent>* event) override {}
};

class SyncEventHandler : public fidl::WireSyncEventHandler<fidl_test::Example> {
  void OnExistingEvent(fidl::WireResponse<fidl_test::Example::OnExistingEvent>* event) override {}
};

void sendEvents(fidl::ServerBindingRef<fidl_test::Example> server) { server->OnExistingEvent(); }
// [END contents]

int main(int argc, const char** argv) { return 0; }
