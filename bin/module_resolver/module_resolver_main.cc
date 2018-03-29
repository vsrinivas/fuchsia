// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/modular.h>
#include <fuchsia/cpp/network.h>

#include <fuchsia/cpp/modular.h>
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/module_resolver/module_resolver_impl.h"
#include "peridot/lib/fidl/equals.h"
#include "peridot/lib/module_manifest_source/directory_source.h"
#include "peridot/lib/module_manifest_source/firebase_source.h"
#include "peridot/lib/module_manifest_source/push_package_source.h"
#include "peridot/public/lib/entity/cpp/json.h"

namespace modular {
namespace {

// NOTE: This must match the path specified in
// peridot/build/module_repository/manifest_package.gni
constexpr char kReadOnlyModuleRepositoryPath[] =
    "/system/data/module_manifest_repository";

constexpr char kContextListenerEntitiesKey[] = "entities";

class ModuleResolverApp : ContextListener {
 public:
  ModuleResolverApp(component::ApplicationContext* const context, bool is_test)
      : app_context_(context), context_listener_binding_(this) {
    modular::ComponentContextPtr component_context;
    app_context_->ConnectToEnvironmentService<modular::ComponentContext>(
        component_context.NewRequest());
    modular::EntityResolverPtr entity_resolver;
    component_context->GetEntityResolver(entity_resolver.NewRequest());

    context->ConnectToEnvironmentService(intelligence_services_.NewRequest());

    intelligence_services_->GetContextReader(context_reader_.NewRequest());

    resolver_impl_ =
        std::make_unique<ModuleResolverImpl>(std::move(entity_resolver));
    // Set up |resolver_impl_|.
    resolver_impl_->AddSource(
        "local_ro", std::make_unique<modular::DirectoryModuleManifestSource>(
                        kReadOnlyModuleRepositoryPath, false /* create */));
    if (!is_test) {
      resolver_impl_->AddSource(
          "firebase_mods",
          std::make_unique<modular::FirebaseModuleManifestSource>(
              fsl::MessageLoop::GetCurrent()->task_runner(),
              [context]() {
                network::NetworkServicePtr network_service;
                context->ConnectToEnvironmentService(
                    network_service.NewRequest());
                return network_service;
              },
              "cloud-mods", "" /* prefix */));
      resolver_impl_->AddSource(
          "push_package",
          std::make_unique<modular::PushPackageSource>(context));
    }

    // Make |resolver_impl_| a query (ask) handler.
    fidl::InterfaceHandle<QueryHandler> query_handler;
    resolver_impl_->BindQueryHandler(query_handler.NewRequest());
    intelligence_services_->RegisterQueryHandler(std::move(query_handler));

    intelligence_services_->GetProposalPublisher(
        proposal_publisher_.NewRequest());

    ContextQuery query;
    ContextSelector selector;
    selector.type = ContextValueType::ENTITY;
    ContextQueryEntry selector_entry;
    selector_entry.key = kContextListenerEntitiesKey;
    selector_entry.value = std::move(selector);
    fidl::VectorPtr<ContextQueryEntry> selector_array;
    selector_array.push_back(std::move(selector_entry));
    query.selector = std::move(selector_array);
    context_reader_->Subscribe(std::move(query),
                               context_listener_binding_.NewBinding());

    context->outgoing_services()->AddService<modular::ModuleResolver>(
        [this](fidl::InterfaceRequest<modular::ModuleResolver> request) {
          resolver_impl_->Connect(std::move(request));
        });
  }

  void Terminate(const std::function<void()>& done) { done(); }

  void OnContextUpdate(ContextUpdate update) override {
    fidl::VectorPtr<ContextValue> values;
    for (auto& entry : *update.values) {
      if (entry.key == kContextListenerEntitiesKey) {
        values = std::move(entry.value);
        break;
      }
    }
    if (values->empty()) {
      return;
    }

    modular::ResolverQuery query;
    // The story id to be extracted from the context update.
    std::string story_id;

    for (const auto& value : *values) {
      if (!value.meta.story || !value.meta.link || !value.meta.entity) {
        continue;
      }
      story_id = value.meta.story->id;

      query.noun_constraints.push_back(
          CreateResolverNounConstraintFromContextValue(value));
    }

    resolver_impl_->FindModules(
        std::move(query),
        [this, story_id](const modular::FindModulesResult& result) {
          std::vector<Proposal> new_proposals;
          std::vector<modular::DaisyPtr> new_daisies;  // Only for comparison.
          int proposal_count = 0;
          for (const auto& module : *result.modules) {
            modular::DaisyPtr daisy;
            new_proposals.push_back(CreateProposalFromModuleResolverResult(
                module, story_id, proposal_count++, &daisy));
            new_daisies.push_back(std::move(daisy));
          }

          // Compare the old daisies and the new daisies. This is a proxy
          // for comparing the set of proposals themselves, because proposals
          // cannot be cloned, which makes it hard to compare them.
          bool push_new_proposals = true;
          if (new_daisies.size() == current_proposal_daisies_.size()) {
            push_new_proposals = false;
            for (uint32_t i = 0; i < new_daisies.size(); ++i) {
              if (!DaisyEqual(new_daisies[i], current_proposal_daisies_[i])) {
                push_new_proposals = true;
                break;
              }
            }
          }

          if (push_new_proposals) {
            // Make sure to remove any existing proposal before creating new
            // ones. This
            // is outside the find call to make sure that stale suggestions are
            // cleared regardless of the resolver results.
            for (const auto& proposal_id : current_proposal_ids_) {
              proposal_publisher_->Remove(proposal_id);
            }
            current_proposal_ids_.clear();
            for (uint32_t i = 0; i < new_proposals.size(); ++i) {
              current_proposal_ids_.push_back(new_proposals[i].id);
              proposal_publisher_->Propose(std::move(new_proposals[i]));
            }
            current_proposal_daisies_ = std::move(new_daisies);
          }
        });
  }

  // Creates a new proposal from the contents of the provided module resolver
  // result.
  //
  // |story_id| is the id of the story that the proposal should add modules to.
  // |proposal_id| is the id of the created proposal, which will also be cached
  // in |current_proposal_ids_|.
  Proposal CreateProposalFromModuleResolverResult(
      const modular::ModuleResolverResult& module_result,
      const std::string& story_id,
      int proposal_id,
      modular::DaisyPtr* daisy_out) {
    modular::Daisy daisy;
    daisy.url = module_result.module_id;
    fidl::VectorPtr<modular::NounEntry> nouns;
    for (const modular::ChainEntry& chain_entry :
         *module_result.create_chain_info.property_info) {
      modular::NounEntry noun_entry;
      noun_entry.name = chain_entry.key;
      modular::Noun noun;
      const modular::CreateChainPropertyInfo& create_chain_info =
          chain_entry.value;
      if (create_chain_info.is_link_path()) {
        modular::LinkPath link_path;
        fidl::Clone(create_chain_info.link_path(), &link_path);
        noun.set_link_path(std::move(link_path));
      } else if (create_chain_info.is_create_link()) {
        noun.set_entity_reference(create_chain_info.create_link().initial_data);
      }
      noun_entry.noun = std::move(noun);
      nouns.push_back(std::move(noun_entry));
    }
    daisy.nouns = std::move(nouns);

    AddModule add_module;
    fidl::Clone(daisy, daisy_out->get());
    add_module.daisy = std::move(daisy);
    add_module.module_name = module_result.module_id;
    add_module.story_id = story_id;
    Action action;
    action.set_add_module(std::move(add_module));

    Proposal proposal;
    proposal.id = std::to_string(proposal_id);
    proposal.on_selected.push_back(std::move(action));

    SuggestionDisplay display;
    if (module_result.manifest && module_result.manifest->suggestion_headline) {
      display.headline = module_result.manifest->suggestion_headline;
      display.subheadline = module_result.module_id;
    } else {
      display.headline = module_result.module_id;
    }
    display.color = 0x00aa00aa;  // argb purple
    display.annoyance = AnnoyanceType::NONE;
    proposal.display = std::move(display);

    return proposal;
  }

  // Creates a resolver noun constraint from the contents of the context value.
  //
  // |value| must contain |entity| and |link| in its |meta|. This is to ensure
  // that link_info can be constructed for the noun constraint.
  modular::ResolverNounConstraintEntry
  CreateResolverNounConstraintFromContextValue(const ContextValue& value) {
    fidl::VectorPtr<fidl::StringPtr> entity_types =
        value.meta.entity->type.Clone();
    const LinkMetadataPtr& link_metadata = value.meta.link;

    modular::ResolverLinkInfo link_info;
    modular::LinkPath link_path;
    link_path.module_path = link_metadata->module_path.Clone();
    link_path.link_name = link_metadata->name;
    link_info.path = std::move(link_path);

    modular::LinkAllowedTypes link_allowed_types;
    link_allowed_types.allowed_entity_types = std::move(entity_types);
    link_info.allowed_types = fidl::MakeOptional(std::move(link_allowed_types));

    modular::ResolverNounConstraint noun_constraint;
    noun_constraint.set_link_info(std::move(link_info));

    modular::ResolverNounConstraintEntry noun_constraint_entry;
    noun_constraint_entry.key = link_metadata->name;
    noun_constraint_entry.constraint = std::move(noun_constraint);
    return noun_constraint_entry;
  }

 private:
  std::unique_ptr<ModuleResolverImpl> resolver_impl_;

  // The proposal publisher that is used to make proposals based on the current
  // context.
  ProposalPublisherPtr proposal_publisher_;

  // A vector of the ids last passed to the proposal publisher.
  std::vector<std::string> current_proposal_ids_;
  // Used to compare the old proposal to the new proposals.
  // NOTE(thatguy): This is only necessary because context can change
  // frequently but not result in new proposals, causing churn in the
  // "Next" section of suggestions at a high rate.
  std::vector<modular::DaisyPtr> current_proposal_daisies_;

  IntelligenceServicesPtr intelligence_services_;

  component::ApplicationContext* const app_context_;

  ContextReaderPtr context_reader_;
  fidl::Binding<ContextListener> context_listener_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleResolverApp);
};

}  // namespace
}  // namespace modular

const char kUsage[] = R"USAGE(%s [--test])USAGE";

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    printf(kUsage, argv[0]);
    return 0;
  }
  auto is_test = command_line.HasOption("test");
  auto context = component::ApplicationContext::CreateFromStartupInfo();
  modular::AppDriver<modular::ModuleResolverApp> driver(
      context->outgoing_services(),
      std::make_unique<modular::ModuleResolverApp>(context.get(), is_test),
      [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
