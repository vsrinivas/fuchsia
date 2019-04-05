// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_MODULE_RESOLVER_LOCAL_MODULE_RESOLVER_H_
#define PERIDOT_BIN_MODULE_RESOLVER_LOCAL_MODULE_RESOLVER_H_

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include <lib/fidl/cpp/binding_set.h>
#include <src/lib/fxl/memory/weak_ptr.h>

#include "peridot/lib/module_manifest_source/module_manifest_source.h"

namespace modular {

class LocalModuleResolver : fuchsia::modular::ModuleResolver {
 public:
  LocalModuleResolver();
  ~LocalModuleResolver() override;

  // Adds a source of Module manifests to index. It is not allowed to call
  // AddSource() after Connect(). |name| must be unique.
  void AddSource(std::string name, std::unique_ptr<ModuleManifestSource> repo);

  void Connect(
      fidl::InterfaceRequest<fuchsia::modular::ModuleResolver> request);

  // |ModuleResolver|
  void FindModules(fuchsia::modular::FindModulesQuery query,
                   FindModulesCallback callback) override;

 private:
  class FindModulesCall;
  class FindModulesByTypesCall;

  using ModuleUri = std::string;
  using RepoSource = std::string;
  // We use the module URI to identify the module manifest.
  using ManifestId = std::tuple<RepoSource, ModuleUri>;
  using ParameterName = std::string;
  using ParameterType = std::string;
  using Action = std::string;

  std::set<ManifestId> FindHandlers(ModuleUri handler);

  void OnSourceIdle(const std::string& source_name);
  void OnNewManifestEntry(const std::string& source_name, std::string id_in,
                          fuchsia::modular::ModuleManifest new_entry);
  void OnRemoveManifestEntry(const std::string& source_name, std::string id_in);

  void PeriodicCheckIfSourcesAreReady();

  bool AllSourcesAreReady() const {
    return ready_sources_.size() == sources_.size();
  }

  // TODO(thatguy): At some point, factor the index functions out of
  // LocalModuleResolver so that they can be re-used by the general all-modules
  // Ask handler.
  std::map<std::string, std::unique_ptr<ModuleManifestSource>> sources_;
  // Set of sources that have told us they are idle, meaning they have
  // sent us all manifests they knew about at construction time.
  std::set<std::string> ready_sources_;
  // Map of module manifest ID -> module manifest.
  using ManifestMap = std::map<ManifestId, fuchsia::modular::ModuleManifest>;
  ManifestMap manifests_;

  // action -> key in |manifests_|
  std::map<Action, std::set<ManifestId>> action_to_manifests_;
  // (parameter type, parameter name) -> key in |manifests_|
  std::map<std::pair<ParameterType, ParameterName>, std::set<ManifestId>>
      parameter_type_and_name_to_manifests_;
  //  (parameter type) -> keys in |manifests_|.
  std::map<ParameterType, std::set<ManifestId>> parameter_type_to_manifests_;

  fidl::BindingSet<fuchsia::modular::ModuleResolver> bindings_;
  // These are buffered until AllSourcesAreReady() == true.
  std::vector<fidl::InterfaceRequest<fuchsia::modular::ModuleResolver>>
      pending_bindings_;

  bool already_checking_if_sources_are_ready_;

  OperationCollection operations_;

  fxl::WeakPtrFactory<LocalModuleResolver> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LocalModuleResolver);
};

}  // namespace modular

#endif  // PERIDOT_BIN_MODULE_RESOLVER_LOCAL_MODULE_RESOLVER_H_
