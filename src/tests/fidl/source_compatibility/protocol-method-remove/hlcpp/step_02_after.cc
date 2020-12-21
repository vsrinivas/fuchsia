// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/protocolmethodremove/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::protocolmethodremove;

// [START contents]
class Server : public fidl_test::Example {
  void ExistingMethod() final {}
};

void client(fidl_test::ExamplePtr client) { client->ExistingMethod(); }
// [END contents]

int main(int argc, const char** argv) { return 0; }
