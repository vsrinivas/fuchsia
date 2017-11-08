// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_MODULE_RESOLVER_MODULE_RESOLVER_IMPL_H_
#define PERIDOT_BIN_MODULE_RESOLVER_MODULE_RESOLVER_IMPL_H_

#include <functional>
#include <memory>
#include <vector>

#include "lib/module_resolver/fidl/module_resolver.fidl.h"

#include "garnet/public/lib/fidl/cpp/bindings/binding_set.h"
#include "peridot/lib/module_manifest_repository/module_manifest_repository.h"

namespace maxwell {

class ModuleResolverImpl : modular::ModuleResolver {
 public:
  ModuleResolverImpl();
  ~ModuleResolverImpl() override;

  // Adds a source of Module manifests to index. It is not allowed to call
  // AddRepository() after Connect(). |name| must be unique.
  void AddRepository(std::string name,
                     std::unique_ptr<modular::ModuleManifestRepository> repo);

  void Connect(fidl::InterfaceRequest<modular::ModuleResolver> request);

 private:
  // repo name, entry id
  using EntryId = std::pair<std::string, std::string>;

  // |ModuleResolver|
  void FindModules(modular::DaisyPtr daisy,
                   modular::ResolverScoringInfoPtr scoring_info,
                   const FindModulesCallback& done) override;

  void OnNewManifestEntry(std::string repo_name,
                          std::string id,
                          modular::ModuleManifestRepository::Entry entry);
  void OnRemoveManifestEntry(std::string repo_name, std::string id);

  // TODO(thatguy): At some point, factor the index functions out of
  // ModuleResolverImpl so that they can be re-used by the general all-modules
  // Ask handler.
  std::vector<std::unique_ptr<modular::ModuleManifestRepository>> repositories_;
  // Map of (repo name, entry name) -> entry.
  std::map<EntryId, modular::ModuleManifestRepository::Entry>
      entries_;

  // verb -> key in |entries_|
  std::map<std::string, std::set<EntryId>> verb_to_entry_;
  // (type, noun name) -> key in |entries_|
  std::map<std::pair<std::string, std::string>, std::set<EntryId>>
      noun_type_to_entry_;

  fidl::BindingSet<modular::ModuleResolver> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleResolverImpl);
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_MODULE_RESOLVER_MODULE_RESOLVER_IMPL_H_
