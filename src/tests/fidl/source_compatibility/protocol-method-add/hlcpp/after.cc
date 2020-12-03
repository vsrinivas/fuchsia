// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/addmethod/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::addmethod;

class Impl final : public fidl_test::ExampleProtocol {
  void ExistingMethod() final {}
  void NewMethod() final {}
};

int main(int argc, const char** argv) {
  Impl server{};
  return 0;
}
