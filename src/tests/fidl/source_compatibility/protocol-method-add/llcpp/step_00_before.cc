// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/client.h>

#include <fidl/test/protocolmethodadd/llcpp/fidl.h>  // nogncheck
namespace fidl_test = llcpp::fidl::test::protocolmethodadd;

// [START contents]
class Server final : public fidl_test::Example::Interface {
 public:
  void ExistingMethod(ExistingMethodCompleter::Sync& completer) final {}
};

void client(fidl::Client<fidl_test::Example> client) { client->ExistingMethod(); }
// [END contents]

int main(int argc, const char** argv) { return 0; }
