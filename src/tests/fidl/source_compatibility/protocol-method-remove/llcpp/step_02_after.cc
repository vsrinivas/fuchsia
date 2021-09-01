// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.protocolmethodremove/cpp/wire.h>  // nogncheck
#include <lib/fidl/llcpp/client.h>
namespace fidl_test = fidl_test_protocolmethodremove;

// [START contents]
class Server final : public fidl::WireServer<fidl_test::Example> {
 public:
  void ExistingMethod(ExistingMethodRequestView request,
                      ExistingMethodCompleter::Sync& completer) final {}
};

void client(fidl::WireClient<fidl_test::Example> client) { client->ExistingMethod(); }
// [END contents]

int main(int argc, const char** argv) { return 0; }
