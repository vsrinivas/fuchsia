// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/addmethod/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::addmethod;

class ExampleImpl : public fidl_test::Example {
  void ExistingMethod() final {}
  void NewMethod() final {}
};

int main(int argc, const char** argv) {
  ExampleImpl addmethod{};
  return 0;
}
