// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.protocolmethodadd/cpp/wire.h>  // nogncheck
#include <lib/fidl/cpp/wire/client.h>
namespace fidl_test = fidl_test_protocolmethodadd;

// [START contents]
class Server final : public fidl::WireServer<fidl_test::Example> {
 public:
  void ExistingMethod(ExistingMethodCompleter::Sync& completer) final {}
};

void client(fidl::WireClient<fidl_test::Example> client) {
  auto result = client->ExistingMethod();
  ZX_ASSERT(result.ok());
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
