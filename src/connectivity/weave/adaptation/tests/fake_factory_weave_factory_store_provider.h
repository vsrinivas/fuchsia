// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_FACTORY_WEAVE_FACTORY_STORE_PROVIDER_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_FACTORY_WEAVE_FACTORY_STORE_PROVIDER_H_

#include <fuchsia/factory/cpp/fidl_test_base.h>

#include <gtest/gtest.h>

#include "fake_directory.h"

namespace weave::adaptation::testing {

// Fake implementation of the fuchsia.factory.FactoryStoreProvider interface.
class FakeFactoryWeaveFactoryStoreProvider final
    : public fuchsia::factory::testing::WeaveFactoryStoreProvider_TestBase {
 public:
  // Replaces all unimplemented functions with a fatal error.
  void NotImplemented_(const std::string& name) override { FAIL() << name; }

  // Returns a directory containing the factory data.
  void GetFactoryStore(fidl::InterfaceRequest<fuchsia::io::Directory> directory) override {
    directory_.Serve(std::move(directory), dispatcher_);
  }

  // Returns an interface request handler to attach to a service directory.
  fidl::InterfaceRequestHandler<fuchsia::factory::WeaveFactoryStoreProvider> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    dispatcher_ = dispatcher;
    return [this, dispatcher](
               fidl::InterfaceRequest<fuchsia::factory::WeaveFactoryStoreProvider> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  // Closes the binding, simulating the service going away.
  void Close(zx_status_t epitaph_value = ZX_OK) { binding_.Close(epitaph_value); }

  // Set the directory attached to this factory store provider.
  FakeFactoryWeaveFactoryStoreProvider& set_directory(FakeDirectory& directory) {
    directory_ = std::move(directory);
    return *this;
  }

  // Get the directory attached to this factory store provider.
  FakeDirectory& directory() { return directory_; }

 private:
  fidl::Binding<fuchsia::factory::WeaveFactoryStoreProvider> binding_{this};
  async_dispatcher_t* dispatcher_;
  FakeDirectory directory_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_FACTORY_WEAVE_FACTORY_STORE_PROVIDER_H_
