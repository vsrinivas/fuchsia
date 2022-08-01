// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.protocolmethodremove/cpp/wire.h>  // nogncheck
#include <lib/fidl/cpp/wire/client.h>
namespace fidl_test = fidl_test_protocolmethodremove;

// [START contents]
class Server final : public fidl::WireServer<fidl_test::Example> {
 public:
  void ExistingMethod(ExistingMethodRequestView request,
                      ExistingMethodCompleter::Sync& completer) final {}
  void OldMethod(OldMethodRequestView request, OldMethodCompleter::Sync& completer) final {}
};

void client(fidl::WireClient<fidl_test::Example> client) {
  auto result1 = client->ExistingMethod();
  ZX_ASSERT(result1.ok());
  auto result2 = client->OldMethod();
  ZX_ASSERT(result2.ok());
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
