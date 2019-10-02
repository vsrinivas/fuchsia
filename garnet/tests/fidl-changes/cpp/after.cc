// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef AFTER

#include <fidl/test/after/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::after;

#else

#include <fidl/test/during/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::during;

#endif

class AddMethodImpl : public fidl_test::AddMethod {
  void ExistingMethod() final {}
  void NewMethod() final {}
};

class RemoveMethodImpl : public fidl_test::RemoveMethod {
  void ExistingMethod() final {}
};

class AddEventImpl : public fidl_test::AddEvent {
  void ExistingMethod() final {}
};

class RemoveEventImpl : public fidl_test::RemoveEvent {
  void ExistingMethod() final {}
};

int main(int argc, const char** argv) {
  AddMethodImpl add_method{};
  RemoveMethodImpl remove_method{};
  AddEventImpl add_event{};
  RemoveEventImpl remove_event{};
  return 0;
}
