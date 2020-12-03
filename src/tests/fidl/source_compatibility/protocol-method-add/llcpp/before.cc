// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/addmethod/llcpp/fidl.h>  // nogncheck
namespace fidl_test = llcpp::fidl::test::addmethod;

class Impl final : public fidl_test::ExampleProtocol::Interface {
 public:
  void ExistingMethod(ExistingMethodCompleter::Sync& completer) final {}
};

int main(int argc, const char** argv) {
  Impl server{};
  return 0;
}
