// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/app/cpp/connect.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/entity/cpp/json.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/module_resolver/local_module_resolver.h"
#include "peridot/lib/module_manifest_source/firebase_source.h"
#include "peridot/lib/module_manifest_source/module_package_source.h"

namespace modular {
namespace {

namespace http = ::fuchsia::net::oldhttp;

constexpr char kContextListenerEntitiesKey[] = "entities";

class ModuleResolverApp : fuchsia::modular::ContextListener {
 public:
  ModuleResolverApp(fuchsia::sys::StartupContext* const context, bool is_test)
      : context_(context), context_listener_binding_(this) {
    fuchsia::modular::ComponentContextPtr component_context;
    context_->ConnectToEnvironmentService<fuchsia::modular::ComponentContext>(
        component_context.NewRequest());
    context->ConnectToEnvironmentService(intelligence_services_.NewRequest());

    intelligence_services_->GetContextReader(context_reader_.NewRequest());

    resolver_impl_ = std::make_unique<LocalModuleResolver>();
    // Set up |resolver_impl_|.
    resolver_impl_->AddSource("module_package",
                              std::make_unique<ModulePackageSource>(context));
    if (!is_test) {
      resolver_impl_->AddSource(
          "firebase_mods",
          std::make_unique<FirebaseModuleManifestSource>(
              async_get_default(),
              [context]() {
                http::HttpServicePtr http_service;
                context->ConnectToEnvironmentService(http_service.NewRequest());
                return http_service;
              },
              "cloud-mods", "" /* prefix */));
    }

    // Make |resolver_impl_| a query (ask) handler.
    fidl::InterfaceHandle<fuchsia::modular::QueryHandler> query_handler;
    resolver_impl_->BindQueryHandler(query_handler.NewRequest());
    intelligence_services_->RegisterQueryHandler(std::move(query_handler));

    intelligence_services_->GetProposalPublisher(
        proposal_publisher_.NewRequest());

    fuchsia::modular::ContextQuery query;
    fuchsia::modular::ContextSelector selector;
    selector.type = fuchsia::modular::ContextValueType::ENTITY;
    fuchsia::modular::ContextQueryEntry selector_entry;
    selector_entry.key = kContextListenerEntitiesKey;
    selector_entry.value = std::move(selector);
    fidl::VectorPtr<fuchsia::modular::ContextQueryEntry> selector_array;
    selector_array.push_back(std::move(selector_entry));
    query.selector = std::move(selector_array);
    context_reader_->Subscribe(std::move(query),
                               context_listener_binding_.NewBinding());

    context->outgoing().AddPublicService<fuchsia::modular::ModuleResolver>(
        [this](
            fidl::InterfaceRequest<fuchsia::modular::ModuleResolver> request) {
          resolver_impl_->Connect(std::move(request));
        });
  }

  void Terminate(const std::function<void()>& done) { done(); }

 private:
  using QueryParamName = std::string;

  // |ContextListener|
  void OnContextUpdate(fuchsia::modular::ContextUpdate update) override {
    fidl::VectorPtr<fuchsia::modular::ContextValue> values;
    for (auto& entry : *update.values) {
      if (entry.key == kContextListenerEntitiesKey) {
        values = std::move(entry.value);
        break;
      }
    }
    if (values->empty()) {
      return;
    }

    fuchsia::modular::FindModulesByTypesQuery query;
    // The story id to be extracted from the context update.
    std::string story_id;

    std::map<QueryParamName, fuchsia::modular::LinkMetadata> remember;
    for (const auto& value : *values) {
      if (!value.meta.story || !value.meta.link || !value.meta.entity) {
        continue;
      }
      story_id = value.meta.story->id;
      value.meta.link->Clone(&remember[value.meta.link->name]);
      query.parameter_constraints.resize(0);
      query.parameter_constraints.push_back(
          CreateParameterConstraintFromContextValue(value));
    }

    resolver_impl_->FindModulesByTypes(
        std::move(query),
        fxl::MakeCopyable(
            [this, story_id, remember = std::move(remember)](
                const fuchsia::modular::FindModulesByTypesResponse&
                    response) mutable {
              std::vector<fuchsia::modular::Proposal> new_proposals;
              std::vector<fuchsia::modular::Intent>
                  new_intents;  // Only for comparison.
              int proposal_count = 0;
              for (const auto& module_result : *response.results) {
                fuchsia::modular::Intent intent;
                new_proposals.push_back(CreateProposalFromModuleResolverResult(
                    module_result, story_id, proposal_count++, remember,
                    &intent));
                new_intents.push_back(std::move(intent));
              }

              // This is a proxy for comparing the set of proposals themselves,
              // because proposals cannot be cloned, which makes it hard to
              // compare them.
              if (new_intents == current_proposal_intents_) {
                return;
              }
              // Make sure to remove any existing proposal before creating new
              // ones. This is outside the find call to make sure that stale
              // suggestions are cleared regardless of the resolver results.
              for (const auto& proposal_id : current_proposal_ids_) {
                proposal_publisher_->Remove(proposal_id);
              }
              current_proposal_ids_.clear();
              for (uint32_t i = 0; i < new_proposals.size(); ++i) {
                current_proposal_ids_.push_back(new_proposals[i].id);
                proposal_publisher_->Propose(std::move(new_proposals[i]));
              }
              current_proposal_intents_ = std::move(new_intents);
            }));
  }

  // Creates a new proposal from the contents of the provided module resolver
  // result.
  //
  // |story_id| is the id of the story that the proposal should add modules to.
  // |proposal_id| is the id of the created proposal, which will also be cached
  // in |current_proposal_ids_|.
  fuchsia::modular::Proposal CreateProposalFromModuleResolverResult(
      const fuchsia::modular::FindModulesByTypesResult& module_result,
      const std::string& story_id, int proposal_id,
      const std::map<QueryParamName, fuchsia::modular::LinkMetadata>& remember,
      fuchsia::modular::Intent* intent_out) {
    fuchsia::modular::Intent intent;
    intent.action.handler = module_result.module_id;
    fidl::VectorPtr<fuchsia::modular::IntentParameter> parameters;
    fidl::VectorPtr<fidl::StringPtr> parent_mod_path;

    for (const auto& mapping : *module_result.parameter_mappings) {
      fuchsia::modular::IntentParameter parameter;
      parameter.name = mapping.result_param_name;

      const auto& metadata = remember.at(mapping.query_constraint_name.get());
      fuchsia::modular::LinkPath link_path;
      link_path.module_path = metadata.module_path.Clone();
      link_path.link_name = metadata.name;

      parameter.data.set_link_path(std::move(link_path));
      parameters.push_back(std::move(parameter));

      if (!parent_mod_path) {
        // TODO(thatguy): Mod parent-child relationships are critical for the
        // story shell, and right now the Framework only guarantees mod
        // startup ordering based only on Module parent-child relationships:
        // parent mods are always restarted before child mods. The Story
        // Shell relies on this ordering to be deterministic: if we added
        // modA before modB the first time around when creating the story,
        // modB *must* be a descendant of modA. This method of using the
        // link's module_path of the first link-based parameter we find
        // expresses, in short "use the owner of the first shared link
        // between this mod and another mod as the parent".
        // MS-1473
        parent_mod_path = metadata.module_path.Clone();
      }
    }
    intent.parameters = std::move(parameters);

    fuchsia::modular::AddModule add_module;
    fidl::Clone(intent, intent_out);
    add_module.intent = std::move(intent);
    add_module.module_name = module_result.module_id;
    add_module.story_id = story_id;
    add_module.surface_parent_module_path = std::move(parent_mod_path);
    fuchsia::modular::Action action;
    action.set_add_module(std::move(add_module));

    fuchsia::modular::Proposal proposal;
    proposal.id = std::to_string(proposal_id);
    proposal.on_selected.push_back(std::move(action));

    fuchsia::modular::SuggestionDisplay display;
    if (module_result.manifest && module_result.manifest->suggestion_headline) {
      display.headline = module_result.manifest->suggestion_headline;
      display.subheadline = module_result.module_id;
    } else {
      display.headline = module_result.module_id;
    }
    display.color = 0x00aa00aa;  // argb purple
    display.annoyance = fuchsia::modular::AnnoyanceType::NONE;
    proposal.display = std::move(display);

    return proposal;
  }

  // Creates a resolver parameter constraint from the contents of the context
  // value.
  //
  // |value| must contain |entity| and |link| in its |meta|. This is to ensure
  // that link_info can be constructed for the parameter constraint.
  fuchsia::modular::FindModulesByTypesParameterConstraint
  CreateParameterConstraintFromContextValue(
      const fuchsia::modular::ContextValue& value) {
    fuchsia::modular::FindModulesByTypesParameterConstraint entry;
    entry.constraint_name = value.meta.link->name;
    entry.param_types = value.meta.entity->type.Clone();
    return entry;
  }

 private:
  std::unique_ptr<LocalModuleResolver> resolver_impl_;

  // The proposal publisher that is used to make proposals based on the current
  // context.
  fuchsia::modular::ProposalPublisherPtr proposal_publisher_;

  // A vector of the ids last passed to the proposal publisher.
  std::vector<std::string> current_proposal_ids_;
  // Used to compare the old proposal to the new proposals.
  // NOTE(thatguy): This is only necessary because context can change
  // frequently but not result in new proposals, causing churn in the
  // "Next" section of suggestions at a high rate.
  std::vector<fuchsia::modular::Intent> current_proposal_intents_;

  fuchsia::modular::IntelligenceServicesPtr intelligence_services_;

  fuchsia::sys::StartupContext* const context_;

  fuchsia::modular::ContextReaderPtr context_reader_;
  fidl::Binding<fuchsia::modular::ContextListener> context_listener_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleResolverApp);
};

}  // namespace
}  // namespace modular

const char kUsage[] = R"USAGE(%s [--test])USAGE";

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    printf(kUsage, argv[0]);
    return 0;
  }
  auto is_test = command_line.HasOption("test");
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<modular::ModuleResolverApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular::ModuleResolverApp>(context.get(), is_test),
      [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
