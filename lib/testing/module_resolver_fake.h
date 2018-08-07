// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_MODULE_RESOLVER_FAKE_H_
#define PERIDOT_LIB_TESTING_MODULE_RESOLVER_FAKE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

namespace modular {

class ModuleResolverFake : fuchsia::modular::ModuleResolver {
 public:
  ModuleResolverFake();
  ~ModuleResolverFake() override;

  // |ModuleResolver|
  void FindModules(fuchsia::modular::FindModulesQuery query,
                   FindModulesCallback callback) override;

  // |ModuleResolver|
  void GetModuleManifest(fidl::StringPtr module_id,
                         GetModuleManifestCallback callback) override;

  // |ModuleResolver|
  void FindModulesByTypes(fuchsia::modular::FindModulesByTypesQuery query,
                          FindModulesByTypesCallback callback) override;

  void Connect(
      fidl::InterfaceRequest<fuchsia::modular::ModuleResolver> request);

  // Sets the manifest for GetModuleManifest.
  void SetManifest(fuchsia::modular::ModuleManifestPtr manifest);

  // Adds a result to the FindModules response.
  void AddFindModulesResult(fuchsia::modular::FindModulesResult result);

  // Sets a function for validation of the query when calling FindModules. This
  // is useful to intercept the query and ensure it was built as expected.
  void SetFindModulesValidation(
      std::function<void(const fuchsia::modular::FindModulesQuery&)> fn);

  // Sets a function for validation of the query when calling GetModuleManifest.
  // This is useful to intercept the query and ensure it was built as expected.
  void SetGetModuleManifestValidation(
      std::function<void(const fidl::StringPtr&)> fn);

 private:
  fidl::BindingSet<fuchsia::modular::ModuleResolver> bindings_;
  std::function<void(const fuchsia::modular::FindModulesQuery&)>
      find_modules_validate_fn_;
  std::function<void(const fidl::StringPtr&)> get_module_manifest_validate_fn_;
  fuchsia::modular::ModuleManifestPtr manifest_;
  fuchsia::modular::FindModulesResponse find_modules_response_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_MODULE_RESOLVER_FAKE_H_
