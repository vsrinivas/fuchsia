// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef AFTER

#include <fidl/test/after/cpp/fidl.h>
using namespace fidl::test::after;

#else

#include <fidl/test/during/cpp/fidl.h>
using namespace fidl::test::during;

#endif

class AddMethodImpl : public AddMethod {
  virtual void ExistingMethod() override{};
  virtual void NewMethod() override{};
};

class RemoveMethodImpl : public RemoveMethod {
  virtual void ExistingMethod() override{};
};

class AddEventImpl : public AddEvent {
  virtual void ExistingMethod() override{};
};

class RemoveEventImpl : public RemoveEvent {
  virtual void ExistingMethod() override{};
};

int main(int argc, const char** argv) {
  AddMethodImpl add_method{};
  RemoveMethodImpl remove_method{};
  AddEventImpl add_event{};
  RemoveEventImpl remove_event{};
  return 0;
}
