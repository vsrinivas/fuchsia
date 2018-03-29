// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_MODULE_RESOLVER_MODULE_RESOLVER_IMPL_H_
#define PERIDOT_BIN_MODULE_RESOLVER_MODULE_RESOLVER_IMPL_H_

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include <fuchsia/cpp/modular.h>
#include "lib/async/cpp/operation.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/module_resolver/type_inference.h"
#include "peridot/lib/module_manifest_source/module_manifest_source.h"

namespace modular {

class ModuleResolverImpl : modular::ModuleResolver, QueryHandler {
 public:
  ModuleResolverImpl(modular::EntityResolverPtr entity_resolver);
  ~ModuleResolverImpl() override;

  // Adds a source of Module manifests to index. It is not allowed to call
  // AddSource() after Connect(). |name| must be unique.
  void AddSource(std::string name,
                 std::unique_ptr<modular::ModuleManifestSource> repo);

  void Connect(fidl::InterfaceRequest<modular::ModuleResolver> request);

  void BindQueryHandler(fidl::InterfaceRequest<QueryHandler> request);

  // Finds modules matching |query|.
  void FindModules(modular::ResolverQuery query, FindModulesCallback done);

 private:
  class FindModulesCall;

  // repo name, module manifest ID
  using EntryId = std::pair<std::string, std::string>;

  // |QueryHandler|
  void OnQuery(UserInput query, OnQueryCallback done) override;

  // |ModuleResolver|
  void FindModules(modular::ResolverQuery query,
                   modular::ResolverScoringInfoPtr scoring_info,
                   FindModulesCallback done) override;

  void OnSourceIdle(const std::string& source_name);
  void OnNewManifestEntry(const std::string& source_name,
                          std::string id,
                          modular::ModuleManifest entry);
  void OnRemoveManifestEntry(const std::string& source_name, std::string id);

  void PeriodicCheckIfSourcesAreReady();

  bool AllSourcesAreReady() const {
    return ready_sources_.size() == sources_.size();
  }

  // TODO(thatguy): At some point, factor the index functions out of
  // ModuleResolverImpl so that they can be re-used by the general all-modules
  // Ask handler.
  std::map<std::string, std::unique_ptr<modular::ModuleManifestSource>>
      sources_;
  // Set of sources that have told us they are idle, meaning they have
  // sent us all entries they knew about at construction time.
  std::set<std::string> ready_sources_;
  // Map of (repo name, module manifest ID) -> entry.
  std::map<EntryId, modular::ModuleManifest> entries_;

  // verb -> key in |entries_|
  std::map<std::string, std::set<EntryId>> verb_to_entries_;
  // (type, noun name) -> key in |entries_|
  std::map<std::pair<std::string, std::string>, std::set<EntryId>>
      noun_type_and_name_to_entries_;
  //  (type) -> key in |entries_|.
  std::map<std::string, std::set<EntryId>> noun_type_to_entries_;

  fidl::BindingSet<modular::ModuleResolver> bindings_;
  fidl::Binding<QueryHandler> query_handler_binding_;
  // These are buffered until AllSourcesAreReady() == true.
  std::vector<fidl::InterfaceRequest<ModuleResolver>> pending_bindings_;

  bool already_checking_if_sources_are_ready_;
  NounTypeInferenceHelper type_helper_;

  modular::OperationCollection operations_;

  fxl::WeakPtrFactory<ModuleResolverImpl> weak_factory_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleResolverImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_MODULE_RESOLVER_MODULE_RESOLVER_IMPL_H_
