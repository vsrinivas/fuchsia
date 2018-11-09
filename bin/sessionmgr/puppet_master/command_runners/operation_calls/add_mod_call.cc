// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/add_mod_call.h"

#include <lib/entity/cpp/json.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>

#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/find_modules_call.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/get_link_path_for_parameter_name_call.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/get_types_from_entity_call.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/initialize_chain_call.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/update_mod_call.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/module_manifest/module_facet_reader.h"

namespace modular {

namespace {

class AddModCall : public Operation<fuchsia::modular::ExecuteResult,
                                    fuchsia::modular::ModuleData> {
 public:
  AddModCall(StoryStorage* const story_storage,
             fuchsia::modular::ModuleResolver* const module_resolver,
             fuchsia::modular::EntityResolver* const entity_resolver,
             modular::ModuleFacetReader* const module_facet_reader,
             fidl::VectorPtr<fidl::StringPtr> mod_name,
             fuchsia::modular::Intent intent,
             fuchsia::modular::SurfaceRelationPtr surface_relation,
             fidl::VectorPtr<fidl::StringPtr> surface_parent_mod_name,
             fuchsia::modular::ModuleSource module_source, ResultCall done)
      : Operation("AddModCommandRunner::AddModCall", std::move(done)),
        story_storage_(story_storage),
        module_resolver_(module_resolver),
        entity_resolver_(entity_resolver),
        module_facet_reader_(module_facet_reader),
        mod_name_(std::move(mod_name)),
        intent_(std::move(intent)),
        surface_relation_(std::move(surface_relation)),
        surface_parent_mod_name_(std::move(surface_parent_mod_name)),
        module_source_(module_source) {}

 private:
  void Run() override {
    FlowToken flow{this, &out_result_, &out_module_data_};

    if (!surface_parent_mod_name_) {
      surface_parent_mod_name_.resize(0);
    }

    // Success status by default, it will be update it if an error state is
    // found.
    out_result_.status = fuchsia::modular::ExecuteStatus::OK;

    // If we have an action, we use the module resolver to type-check and
    // resolve the (action, parameter) and the supplied optional handler to a
    // module. If the module resolver doesn't recognize a supplied handler, we
    // forgivingly execute the handler anyway.
    if (!intent_.action.is_null()) {
      AddFindModulesOperation(
          &operation_queue_, story_storage_, module_resolver_, entity_resolver_,
          CloneOptional(intent_), surface_parent_mod_name_.Clone(),
          [this, flow](fuchsia::modular::ExecuteResult result,
                       fuchsia::modular::FindModulesResponse response) {
            if (result.status != fuchsia::modular::ExecuteStatus::OK) {
              out_result_ = std::move(result);
              return;
              // Operation finishes since |flow| goes out of scope.
            }

            // NOTE: leave this as a switch case and omit the default case; the
            // compiler will make sure we're handling all error cases.
            switch (response.status) {
              case fuchsia::modular::FindModulesStatus::SUCCESS: {
                if (response.results->empty()) {
                  out_result_.status =
                      fuchsia::modular::ExecuteStatus::NO_MODULES_FOUND;
                  out_result_.error_message =
                      "Resolution of intent gave zero results.";
                  return;
                  // Operation finishes since |flow| goes out of scope.
                }

                candidate_module_ = std::move(response.results->at(0));
              } break;

              case fuchsia::modular::FindModulesStatus::UNKNOWN_HANDLER: {
                FXL_LOG(INFO)
                    << "Module Resolver does not know about module '"
                    << intent_.handler << "' with action = '" << intent_.action
                    << "'. Going to try using it anyway..";
                candidate_module_.module_id = intent_.handler;
              } break;
            }

            CreateLinks(flow);
          });
    } else {
      // We arrive here if the Intent has a handler, but no action.
      FXL_DCHECK(!intent_.handler.is_null())
          << "Cannot start a module without an action or a handler";
      candidate_module_.module_id = intent_.handler;

      CreateLinks(flow);
    }
  }

  // Create module parameters info and create links.
  void CreateLinks(FlowToken flow) {
    CreateModuleParameterMapInfo(flow, [this, flow] {
      if (out_result_.status != fuchsia::modular::ExecuteStatus::OK) {
        return;
        // Operation finishes since |flow| goes out of scope.
      }
      auto full_module_path = surface_parent_mod_name_.Clone();
      full_module_path->insert(full_module_path->end(), mod_name_->begin(),
                               mod_name_->end());
      AddInitializeChainOperation(
          &operation_queue_, story_storage_, std::move(full_module_path),
          std::move(parameter_info_),
          [this, flow](fuchsia::modular::ExecuteResult result,
                       fuchsia::modular::ModuleParameterMapPtr map) {
            if (result.status != fuchsia::modular::ExecuteStatus::OK) {
              out_result_ = std::move(result);
              return;
              // Operation finishes since |flow| goes out of scope.
            }
            WriteModuleData(flow, std::move(map));
          });
    });
  }

  // On success, populates |parameter_info_|. On failure, |out_result_| contains
  // error reason. Calls |done()| on completion in either case.
  void CreateModuleParameterMapInfo(FlowToken flow,
                                    std::function<void()> done) {
    parameter_info_ = fuchsia::modular::CreateModuleParameterMapInfo::New();

    std::vector<FuturePtr<fuchsia::modular::CreateModuleParameterMapEntry>>
        did_get_entries;
    did_get_entries.reserve(intent_.parameters->size());

    for (auto& param : *intent_.parameters) {
      fuchsia::modular::CreateModuleParameterMapEntry entry;
      entry.key = param.name;

      switch (param.data.Which()) {
        case fuchsia::modular::IntentParameterData::Tag::kEntityReference: {
          fuchsia::modular::CreateLinkInfo create_link;
          fsl::SizedVmo vmo;
          FXL_CHECK(fsl::VmoFromString(
              EntityReferenceToJson(param.data.entity_reference()), &vmo));
          create_link.initial_data = std::move(vmo).ToTransport();
          entry.value.set_create_link(std::move(create_link));
          break;
        }
        case fuchsia::modular::IntentParameterData::Tag::kEntityType: {
          // Create a link, but don't populate it. This is useful in the event
          // that the link is used as an 'output' link. Setting a valid JSON
          // value for null in the vmo.
          fsl::SizedVmo vmo;
          FXL_CHECK(fsl::VmoFromString("null", &vmo));
          fuchsia::modular::CreateLinkInfo create_link;
          create_link.initial_data = std::move(vmo).ToTransport();
          entry.value.set_create_link(std::move(create_link));
          break;
        }
        case fuchsia::modular::IntentParameterData::Tag::kJson: {
          fuchsia::modular::CreateLinkInfo create_link;
          param.data.json().Clone(&create_link.initial_data);
          entry.value.set_create_link(std::move(create_link));
          break;
        }
        case fuchsia::modular::IntentParameterData::Tag::kLinkName: {
          auto did_get_lp = Future<fuchsia::modular::LinkPathPtr>::Create(
              "AddModCommandRunner::AddModCall::did_get_link");
          // TODO(miguelfrde): get rid of using surface_parent_mod_name this
          // way. Maybe INVALID status should be returned here since using
          // this parameter in a StoryCommand doesn't make much sense.
          AddGetLinkPathForParameterNameOperation(
              &operations_, story_storage_, surface_parent_mod_name_.Clone(),
              param.data.link_name(), did_get_lp->Completer());

          did_get_entries.emplace_back(
              did_get_lp->Map([this, param_name = param.name,
                               done](fuchsia::modular::LinkPathPtr link_path) {
                if (!GetWeakPtr()) {
                  return fuchsia::modular::CreateModuleParameterMapEntry{};
                }
                if (!link_path) {
                  // OH NO! bail on this entire function's flow.
                  out_result_.status =
                      fuchsia::modular::ExecuteStatus::INTERNAL_ERROR;
                  out_result_.error_message = fxl::StringPrintf(
                      "Link path does not exist for parameter name = %s",
                      param_name.get().c_str());
                  done();
                  return fuchsia::modular::CreateModuleParameterMapEntry{};
                }
                fuchsia::modular::CreateModuleParameterMapEntry entry;
                entry.key = param_name;
                entry.value.set_link_path(std::move(*link_path));
                return entry;
              }));
          continue;
        }
        case fuchsia::modular::IntentParameterData::Tag::kLinkPath: {
          fuchsia::modular::LinkPath lp;
          param.data.link_path().Clone(&lp);
          entry.value.set_link_path(std::move(lp));
          break;
        }
        case fuchsia::modular::IntentParameterData::Tag::Invalid: {
          out_result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
          out_result_.error_message =
              fxl::StringPrintf("Invalid data for parameter with name: %s",
                                param.name.get().c_str());
          done();
          return;
        }
      }

      auto did_create_entry =
          Future<fuchsia::modular::CreateModuleParameterMapEntry>::
              CreateCompleted(
                  "AddModCommandRunner::FindModulesCall.did_create_entry",
                  std::move(entry));
      did_get_entries.emplace_back(std::move(did_create_entry));
    }

    Wait("AddModCommandRunner::AddModCall::Wait", did_get_entries)
        ->Then([this, done, flow](
                   std::vector<fuchsia::modular::CreateModuleParameterMapEntry>
                       entries) {
          parameter_info_->property_info.reset(std::move(entries));
          done();
        });
  }

  // Write module data
  void WriteModuleData(FlowToken flow,
                       fuchsia::modular::ModuleParameterMapPtr map) {
    fidl::Clone(*map, &out_module_data_.parameter_map);
    out_module_data_.module_url = candidate_module_.module_id;
    out_module_data_.module_path = surface_parent_mod_name_.Clone();
    out_module_data_.module_path->insert(out_module_data_.module_path->end(),
                                         mod_name_->begin(), mod_name_->end());
    out_module_data_.module_source = module_source_;
    out_module_data_.module_stopped = false;
    fidl::Clone(surface_relation_, &out_module_data_.surface_relation);
    out_module_data_.intent =
        std::make_unique<fuchsia::modular::Intent>(std::move(intent_));

    auto write_to_storage_cont = [this, flow]() {
      // Operation stays alive until flow goes out of scope.
      fuchsia::modular::ModuleData module_data;
      out_module_data_.Clone(&module_data);
      story_storage_->WriteModuleData(std::move(module_data))
          ->Then([this, flow] {});
    };

    // If the resolver gave us a module manifest (which is described in the
    // module facet), we don't need to try and read it.
    if (candidate_module_.manifest) {
      fidl::Clone(candidate_module_.manifest,
                  &out_module_data_.module_manifest);
      write_to_storage_cont();
      return;
    }

    module_facet_reader_->GetModuleManifest(
        out_module_data_.module_url,
        [this, flow,
         write_to_storage_cont](fuchsia::modular::ModuleManifestPtr manifest) {
          out_module_data_.module_manifest = std::move(manifest);
          write_to_storage_cont();
        });
  }

  StoryStorage* const story_storage_;                        // Not owned.
  fuchsia::modular::ModuleResolver* const module_resolver_;  // Not owned.
  fuchsia::modular::EntityResolver* const entity_resolver_;  // Not owned.
  modular::ModuleFacetReader* const module_facet_reader_;    // Not owned.
  fidl::VectorPtr<fidl::StringPtr> mod_name_;
  fuchsia::modular::Intent intent_;
  fuchsia::modular::SurfaceRelationPtr surface_relation_;
  fidl::VectorPtr<fidl::StringPtr> surface_parent_mod_name_;
  fuchsia::modular::ModuleSource module_source_;
  fuchsia::modular::FindModulesResult candidate_module_;
  fuchsia::modular::CreateModuleParameterMapInfoPtr parameter_info_;
  fuchsia::modular::ModuleData out_module_data_;
  fuchsia::modular::ExecuteResult out_result_;
  // Used when creating the map info to execute an operation as soon as it
  // arrives.
  OperationCollection operations_;
  // Used to enqueue sub-operations that should be executed sequentially.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AddModCall);
};  // namespace

}  // namespace

void AddAddModOperation(
    OperationContainer* const container, StoryStorage* const story_storage,
    fuchsia::modular::ModuleResolver* const module_resolver,
    fuchsia::modular::EntityResolver* const entity_resolver,
    modular::ModuleFacetReader* const module_facet_reader,
    fidl::VectorPtr<fidl::StringPtr> mod_name, fuchsia::modular::Intent intent,
    fuchsia::modular::SurfaceRelationPtr surface_relation,
    fidl::VectorPtr<fidl::StringPtr> surface_parent_mod_name,
    fuchsia::modular::ModuleSource module_source,
    std::function<void(fuchsia::modular::ExecuteResult,
                       fuchsia::modular::ModuleData)>
        done) {
  container->Add(new AddModCall(
      story_storage, module_resolver, entity_resolver, module_facet_reader,
      std::move(mod_name), std::move(intent), std::move(surface_relation),
      std::move(surface_parent_mod_name), module_source, std::move(done)));
}

}  // namespace modular
