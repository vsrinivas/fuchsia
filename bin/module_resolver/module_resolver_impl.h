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
#include "lib/async/cpp/operation.h"
#include "lib/context/fidl/context_reader.fidl.h"
#include "lib/entity/fidl/entity_resolver.fidl.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/suggestion/fidl/query_handler.fidl.h"
#include "peridot/bin/module_resolver/type_inference.h"
#include "peridot/lib/module_manifest_source/module_manifest_source.h"

namespace maxwell {

class ModuleResolverImpl : modular::ModuleResolver,
                           QueryHandler,
                           ContextListener {
 public:
  ModuleResolverImpl(modular::EntityResolverPtr entity_resolver);
  // Constructs a module resolver that uses the provided ContextReader to
  // monitor for context changes and ...
  // TODO(MI4-802): provide suggestions for modules that are compatible with the
  // current context.
  ModuleResolverImpl(modular::EntityResolverPtr entity_resolver,
                     ContextReaderPtr context_reader);
  ~ModuleResolverImpl() override;

  // Adds a source of Module manifests to index. It is not allowed to call
  // AddSource() after Connect(). |name| must be unique.
  void AddSource(std::string name,
                 std::unique_ptr<modular::ModuleManifestSource> repo);

  void Connect(fidl::InterfaceRequest<modular::ModuleResolver> request);

  void BindQueryHandler(fidl::InterfaceRequest<QueryHandler> request);

 private:
  class FindModulesCall;

  // repo name, module manifest ID
  using EntryId = std::pair<std::string, std::string>;

  // |ModuleResolver|
  void FindModules(modular::ResolverQueryPtr query,
                   modular::ResolverScoringInfoPtr scoring_info,
                   const FindModulesCallback& done) override;

  // |QueryHandler|
  void OnQuery(UserInputPtr query, const OnQueryCallback& done) override;

  // |ContextListener|
  void OnContextUpdate(ContextUpdatePtr update) override;

  void OnSourceIdle(const std::string& source_name);
  void OnNewManifestEntry(const std::string& source_name,
                          std::string id,
                          modular::ModuleManifestSource::Entry entry);
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
  std::map<EntryId, modular::ModuleManifestSource::Entry> entries_;

  // verb -> key in |entries_|
  std::map<std::string, std::set<EntryId>> verb_to_entries_;
  // (type, noun name) -> key in |entries_|
  std::map<std::pair<std::string, std::string>, std::set<EntryId>>
      noun_type_to_entries_;

  fidl::BindingSet<modular::ModuleResolver> bindings_;
  fidl::Binding<QueryHandler> query_handler_binding_;
  // These are buffered until AllSourcesAreReady() == true.
  std::vector<fidl::InterfaceRequest<ModuleResolver>> pending_bindings_;

  bool already_checking_if_sources_are_ready_;
  NounTypeInferenceHelper type_helper_;

  modular::OperationCollection operations_;

  // The context reader that is used to suggest modules compatible with the
  // current context.
  ContextReaderPtr context_reader_;
  fidl::Binding<ContextListener> context_listener_binding_;

  fxl::WeakPtrFactory<ModuleResolverImpl> weak_factory_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleResolverImpl);
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_MODULE_RESOLVER_MODULE_RESOLVER_IMPL_H_
