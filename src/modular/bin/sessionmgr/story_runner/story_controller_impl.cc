// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story_runner/story_controller_impl.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/storymodel/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/add_mod_call.h"
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/find_modules_call.h"
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/get_types_from_entity_call.h"
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/initialize_chain_call.h"
#include "src/modular/bin/sessionmgr/storage/constants_and_utils.h"
#include "src/modular/bin/sessionmgr/storage/story_storage.h"
#include "src/modular/bin/sessionmgr/story/model/story_observer.h"
#include "src/modular/bin/sessionmgr/story_runner/module_context_impl.h"
#include "src/modular/bin/sessionmgr/story_runner/module_controller_impl.h"
#include "src/modular/bin/sessionmgr/story_runner/ongoing_activity_impl.h"
#include "src/modular/bin/sessionmgr/story_runner/story_provider_impl.h"
#include "src/modular/bin/sessionmgr/story_runner/story_shell_context_impl.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/entity/cpp/json.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/ledger_client/operations.h"
#include "src/modular/lib/string_escape/string_escape.h"

namespace modular {

namespace {

constexpr char kSurfaceIDSeparator[] = ":";
std::string ModulePathToSurfaceID(const std::vector<std::string>& module_path) {
  std::vector<std::string> path;
  // Sanitize all the |module_name|s that make up this |module_path|.
  for (const auto& module_name : module_path) {
    path.push_back(StringEscape(module_name, kSurfaceIDSeparator));
  }
  return fxl::JoinStrings(path, kSurfaceIDSeparator);
}

std::vector<std::string> ModulePathFromSurfaceID(const std::string& surface_id) {
  std::vector<std::string> path;
  for (const auto& parts :
       SplitEscapedString(fxl::StringView(surface_id), kSurfaceIDSeparator[0])) {
    path.push_back(parts.ToString());
  }
  return path;
}

std::vector<std::string> ParentModulePath(const std::vector<std::string>& module_path) {
  std::vector<std::string> ret;

  if (module_path.size() > 0) {
    for (size_t i = 0; i < module_path.size() - 1; i++) {
      ret.push_back(module_path.at(i));
    }
  }
  return ret;
}

}  // namespace

bool ShouldRestartModuleForNewIntent(const fuchsia::modular::Intent& old_intent,
                                     const fuchsia::modular::Intent& new_intent) {
  if (old_intent.handler != new_intent.handler) {
    return true;
  }

  return false;
}

zx_time_t GetNowUTC() {
  zx_time_t now = 0u;
  zx_clock_get(ZX_CLOCK_UTC, &now);
  return now;
}

void StoryControllerImpl::RunningModInfo::InitializeInspect(
    StoryControllerImpl* const story_controller_impl) {
  mod_inspect_node =
      story_controller_impl->story_inspect_node_->CreateChild(module_data->module_url());

  std::string is_embedded_str;
  if (module_data->is_embedded()) {
    is_embedded_str = "True";
  } else {
    is_embedded_str = "False";
  }

  is_embedded_property =
      mod_inspect_node.CreateString(modular_config::kInspectIsEmbedded, is_embedded_str);

  std::string is_deleted_str;
  if (module_data->module_deleted()) {
    is_deleted_str = "True";
  } else {
    is_deleted_str = "False";
  }

  is_deleted_property =
      mod_inspect_node.CreateString(modular_config::kInspectIsDeleted, is_deleted_str);

  std::string mod_source_string;
  switch (module_data->module_source()) {
    case fuchsia::modular::ModuleSource::INTERNAL:
      mod_source_string = "INTERNAL";
      break;
    case fuchsia::modular::ModuleSource::EXTERNAL:
      mod_source_string = "EXTERNAL";
      break;
  }

  module_source_property =
      mod_inspect_node.CreateString(modular_config::kInspectModuleSource, mod_source_string);

  module_intent_action_property = mod_inspect_node.CreateString(
      modular_config::kInspectIntentAction, module_data->intent().action.value_or(""));
  module_intent_handler_property = mod_inspect_node.CreateString(
      modular_config::kInspectIntentHandler, module_data->intent().handler.value_or(""));

  std::string formatted_params = "";
  if (module_data->intent().parameters.has_value()) {
    for (auto& param : *module_data->intent().parameters) {
      formatted_params.append("name : " + param.name.value_or("") + " ");
    }
  }
  module_intent_params_property =
      mod_inspect_node.CreateString(modular_config::kInspectIntentParams, formatted_params);

  std::string module_path_str = fxl::JoinStrings(module_data->module_path(), ", ");
  module_path_property =
      mod_inspect_node.CreateString(modular_config::kInspectModulePath, module_path_str);
  if (module_data->has_surface_relation()) {
    std::string arrangement;
    switch (module_data->surface_relation().arrangement) {
      case fuchsia::modular::SurfaceArrangement::COPRESENT:
        arrangement = "COPRESENT";
        break;
      case fuchsia::modular::SurfaceArrangement::SEQUENTIAL:
        arrangement = "SEQUENTIAL";
        break;
      case fuchsia::modular::SurfaceArrangement::ONTOP:
        arrangement = "ONTOP";
        break;
      case fuchsia::modular::SurfaceArrangement::NONE:
        arrangement = "NONE";
        break;
    }
    module_surface_relation_arrangement = mod_inspect_node.CreateString(
        modular_config::kInspectSurfaceRelationArrangement, arrangement);

    std::string dependency;
    switch (module_data->surface_relation().dependency) {
      case fuchsia::modular::SurfaceDependency::DEPENDENT:
        dependency = "DEPENDENT";
        break;
      case fuchsia::modular::SurfaceDependency::NONE:
        dependency = "NONE";
        break;
    }

    module_surface_relation_dependency = mod_inspect_node.CreateString(
        modular_config::kInspectSurfaceRelationDependency, dependency);
    module_surface_relation_emphasis = mod_inspect_node.CreateDouble(
        modular_config::kInspectSurfaceRelationEmphasis, module_data->surface_relation().emphasis);
  }
  ResetInspect();
}

void StoryControllerImpl::RunningModInfo::ResetInspect() {
  module_intent_action_property.Set(module_data->intent().action.value_or(""));
  module_intent_handler_property.Set(module_data->intent().handler.value_or(""));

  std::string param_names_str = "";
  if (module_data->intent().parameters.has_value()) {
    for (auto& param : *module_data->intent().parameters) {
      param_names_str.append("name : " + param.name.value_or("") + " ");
    }
  }
  module_intent_params_property.Set(param_names_str);

  if (module_data->has_annotations()) {
    for (const fuchsia::modular::Annotation& annotation : module_data->annotations()) {
      std::string value_str = modular::annotations::ToInspect(*annotation.value.get());
      std::string key_with_prefix = "annotation: " + annotation.key;
      if (annotation_properties.find(key_with_prefix) != annotation_properties.end()) {
        annotation_properties[key_with_prefix].Set(value_str);
      } else {
        annotation_properties.insert(std::pair<const std::string, inspect::StringProperty>(
            key_with_prefix, mod_inspect_node.CreateString(key_with_prefix, value_str)));
      }
    }
  }
}

// Launches (brings up a running instance) of a module.
//
// If the module is to be composed into the story shell, notifies the story
// shell of the new module. If the module is composed internally, connects the
// view owner request appropriately.
class StoryControllerImpl::LaunchModuleCall : public Operation<> {
 public:
  LaunchModuleCall(
      StoryControllerImpl* const story_controller_impl, fuchsia::modular::ModuleData module_data,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController> module_controller_request,
      std::optional<fuchsia::ui::views::ViewToken> view_token, ResultCall result_call)
      : Operation("StoryControllerImpl::LaunchModuleCall", std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)),
        view_token_(std::move(view_token)),
        module_controller_request_(std::move(module_controller_request)),
        start_time_(GetNowUTC()) {}

 private:
  void Run() override {
    FlowToken flow{this};

    RunningModInfo* const running_mod_info =
        story_controller_impl_->FindRunningModInfo(module_data_.module_path());

    // We launch the new module if it doesn't run yet.
    if (!running_mod_info) {
      Launch(flow);
      return;
    }

    // If the new module is already running, but with a different Intent, we
    // tear it down then launch a new instance.
    if (ShouldRestartModuleForNewIntent(running_mod_info->module_data->intent(),
                                        module_data_.intent())) {
      running_mod_info->module_controller_impl->Teardown([this, flow] {
        // NOTE(mesch): |running_mod_info| is invalid at this point.
        Launch(flow);
      });
      return;
    }

    // Otherwise, the module is already running. Connect
    // |module_controller_request_| to the existing instance of
    // fuchsia::modular::ModuleController.
    if (module_controller_request_.is_valid()) {
      running_mod_info->module_controller_impl->Connect(std::move(module_controller_request_));
    }

    // Since the module is already running send it the new intent.
    NotifyModuleOfIntent(*running_mod_info);
  }

  void Launch(FlowToken /*flow*/) {
    FXL_LOG(INFO) << "StoryControllerImpl::LaunchModule() " << module_data_.module_url() << " "
                  << ModulePathToSurfaceID(module_data_.module_path());
    fuchsia::modular::AppConfig module_config;
    module_config.url = module_data_.module_url();

    fuchsia::sys::ServiceProviderPtr module_context_provider;
    auto module_context_provider_request = module_context_provider.NewRequest();
    auto service_list = fuchsia::sys::ServiceList::New();
    service_list->names.push_back(fuchsia::modular::ComponentContext::Name_);
    service_list->names.push_back(fuchsia::modular::ModuleContext::Name_);
    service_list->names.push_back(fuchsia::intl::PropertyProvider::Name_);
    service_list->provider = std::move(module_context_provider);

    RunningModInfo running_mod_info;
    running_mod_info.module_data = CloneOptional(module_data_);

    // Launch the child application with a newly-constructed ViewTokenPair if
    // no token was already provided.
    if (view_token_) {
      // ModuleControllerImpl's constructor launches the child application.
      running_mod_info.module_controller_impl = std::make_unique<ModuleControllerImpl>(
          story_controller_impl_,
          story_controller_impl_->story_provider_impl_->session_environment()->GetLauncher(),
          std::move(module_config), running_mod_info.module_data.get(), std::move(service_list),
          std::move(*view_token_));
    } else {
      auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

      // ModuleControllerImpl's constructor launches the child application.
      running_mod_info.module_controller_impl = std::make_unique<ModuleControllerImpl>(
          story_controller_impl_,
          story_controller_impl_->story_provider_impl_->session_environment()->GetLauncher(),
          std::move(module_config), running_mod_info.module_data.get(), std::move(service_list),
          std::move(view_token));
      running_mod_info.pending_view_holder_token = std::move(view_holder_token);
    }

    // Modules added/started through PuppetMaster don't have a module
    // controller request.
    if (module_controller_request_.is_valid()) {
      running_mod_info.module_controller_impl->Connect(std::move(module_controller_request_));
    }

    ModuleContextInfo module_context_info = {
        story_controller_impl_->story_provider_impl_->component_context_info(),
        story_controller_impl_,
        story_controller_impl_->story_provider_impl_->session_environment(),
    };

    running_mod_info.module_context_impl =
        std::make_unique<ModuleContextImpl>(module_context_info, running_mod_info.module_data.get(),
                                            std::move(module_context_provider_request));

    running_mod_info.InitializeInspect(story_controller_impl_);

    NotifyModuleOfIntent(running_mod_info);

    story_controller_impl_->running_mod_infos_.emplace_back(std::move(running_mod_info));

    for (auto& i : story_controller_impl_->watchers_.ptrs()) {
      fuchsia::modular::ModuleData module_data;
      module_data_.Clone(&module_data);
      (*i)->OnModuleAdded(std::move(module_data));
    }

    zx_time_t now = 0;
    zx_clock_get(ZX_CLOCK_UTC, &now);
    ReportModuleLaunchTime(module_data_.module_url(), zx::duration(now - start_time_));
  }

  // Connects to the module's intent handler and sends it the intent from
  // |module_data_.intent|.
  void NotifyModuleOfIntent(const RunningModInfo& running_mod_info) {
    if (!module_data_.has_intent()) {
      return;
    }
    fuchsia::modular::IntentHandlerPtr intent_handler;
    running_mod_info.module_controller_impl->services().ConnectToService(
        intent_handler.NewRequest());
    fuchsia::modular::Intent intent;
    module_data_.intent().Clone(&intent);

    intent_handler->HandleIntent(std::move(intent));
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::modular::ModuleData module_data_;
  std::optional<fuchsia::ui::views::ViewToken> view_token_;
  fidl::InterfaceRequest<fuchsia::modular::ModuleController> module_controller_request_;
  const zx_time_t start_time_;
};

// KillModuleCall tears down the module by the given module_data. It is enqueued
// when ledger confirms that the module was stopped, see OnModuleDataUpdated().
class StoryControllerImpl::KillModuleCall : public Operation<> {
 public:
  KillModuleCall(StoryControllerImpl* const story_controller_impl,
                 fuchsia::modular::ModuleData module_data, fit::function<void()> done)
      : Operation("StoryControllerImpl::KillModuleCall", [] {}),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)),
        done_(std::move(done)) {}

 private:
  void Run() override {
    FlowToken flow{this};
    // If the module is external, we also notify story shell about it going
    // away. An internal module is stopped by its parent module, and it's up to
    // the parent module to defocus it first. TODO(mesch): Why not always
    // defocus?
    auto future = Future<>::Create("StoryControllerImpl.KillModuleCall.Run.future");
    if (story_controller_impl_->story_shell_ &&
        module_data_.module_source() == fuchsia::modular::ModuleSource::EXTERNAL) {
      story_controller_impl_->story_shell_->DefocusSurface(
          ModulePathToSurfaceID(module_data_.module_path()), future->Completer());
    } else {
      future->Complete();
    }

    future->Then([this, flow] {
      // Teardown the module, which discards the module controller. Since
      // multiple KillModuleCall operations can be queued by module data
      // updates, we must check whether the module has already been killed.
      auto* const running_mod_info =
          story_controller_impl_->FindRunningModInfo(module_data_.module_path());
      if (!running_mod_info) {
        FXL_LOG(INFO) << "No ModuleController for Module '"
                      << ModulePathToSurfaceID(module_data_.module_path())
                      << "'. Was ModuleController.Stop() called twice?";
        InvokeDone();
        return;
      }

      // The result callback |done_| must be invoked BEFORE the Teardown()
      // callback returns, just in case it is, or it invokes, a callback of a
      // FIDL method on ModuleController (happens in the case that this
      // Operation instance executes a ModuleController.Stop() FIDL method
      // invocation).
      //
      // After the Teardown() callback returns, the ModuleControllerImpl is
      // deleted, and any FIDL connections that have invoked methods on it are
      // closed.
      //
      // Be aware that done_ is NOT the Done() callback of the Operation.
      running_mod_info->module_controller_impl->Teardown([this, flow] { InvokeDone(); });
    });
  }

  void InvokeDone() {
    // Whatever the done_ callback captures (specifically, a flow token) must be
    // released after the done_ callback has returned. Otherwise, the calling
    // operation will not call Done() and does not get deleted until this
    // Operation instance gets deleted. This is probably fine, but it's
    // different from calling operations without flow tokens, which call their
    // own Done() directly.
    //
    // Notice the StopCall doesn't use a flow token, but just calls Done()
    // directly from within done_, but the OnModuleDataUpadatedCall has a flow
    // token.

    // We must guard against the possibility that done_() causes this to be
    // deleted (happens when called from StopCall).
    auto done = std::move(done_);
    done();
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::modular::ModuleData module_data_;
  fit::function<void()> done_;
};

// Calls LaunchModuleCall to get a running instance, and delegates visual
// composition to the story shell.
class StoryControllerImpl::LaunchModuleInShellCall : public Operation<> {
 public:
  LaunchModuleInShellCall(
      StoryControllerImpl* const story_controller_impl, fuchsia::modular::ModuleData module_data,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController> module_controller_request,
      ResultCall result_call)
      : Operation("StoryControllerImpl::LaunchModuleInShellCall", std::move(result_call),
                  module_data.module_url()),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)),
        module_controller_request_(std::move(module_controller_request)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    operation_queue_.Add(std::make_unique<LaunchModuleCall>(
        story_controller_impl_, fidl::Clone(module_data_), std::move(module_controller_request_),
        /*view_token=*/std::nullopt, [this, flow] { LoadModuleManifest(flow); }));
  }

  void LoadModuleManifest(FlowToken flow) {
    story_controller_impl_->story_provider_impl_->module_facet_reader()->GetModuleManifest(
        module_data_.module_url(), [this, flow](fuchsia::modular::ModuleManifestPtr manifest) {
          module_manifest_ = std::move(manifest);
          MaybeConnectViewToStoryShell(flow);
        });
  }

  // We only add a module to story shell if it's either a root module or its
  // anchor module is already known to story shell. Otherwise, we pend its view
  // (StoryControllerImpl::pending_story_shell_views_) and pass it to the story
  // shell once its anchor module is ready.
  void MaybeConnectViewToStoryShell(FlowToken flow) {
    // If this is called during Stop(), story_shell_ might already have been
    // reset. TODO(mesch): Then the whole operation should fail.
    if (!story_controller_impl_->story_shell_) {
      return;
    }

    auto* const running_mod_info =
        story_controller_impl_->FindRunningModInfo(module_data_.module_path());
    FXL_CHECK(running_mod_info);  // This was just created in LaunchModuleCall.

    std::string anchor_surface_id;
    auto* const anchor = story_controller_impl_->FindAnchor(running_mod_info);
    if (anchor) {
      anchor_surface_id = ModulePathToSurfaceID(anchor->module_data->module_path());
    }

    const auto surface_id = ModulePathToSurfaceID(module_data_.module_path());

    fuchsia::modular::ViewConnection view_connection;
    view_connection.surface_id = surface_id;
    view_connection.view_holder_token = std::move(*running_mod_info->pending_view_holder_token);

    fuchsia::modular::SurfaceInfo2 surface_info;
    surface_info.set_parent_id(anchor_surface_id);
    if (module_data_.has_surface_relation()) {
      surface_info.set_surface_relation(fidl::Clone(module_data_.surface_relation()));
    }
    if (module_manifest_) {
      surface_info.set_module_manifest(std::move(*module_manifest_));
    }
    surface_info.set_module_source(module_data_.module_source());

    // If this is a root module, or the anchor module is connected to the story shell,
    // connect this module to the story shell. Otherwise, pend it to connect once the anchor
    // module is ready.
    if (module_data_.module_path().size() == 1 ||
        story_controller_impl_->connected_views_.count(anchor_surface_id)) {
      ConnectViewToStoryShell(flow, std::move(view_connection), std::move(surface_info));
    } else {
      story_controller_impl_->pending_story_shell_views_.emplace(
          ModulePathToSurfaceID(module_data_.module_path()),
          PendingViewForStoryShell{module_data_.module_path(), std::move(view_connection),
                                   std::move(surface_info)});
    }
  }

  void ConnectViewToStoryShell(FlowToken flow, fuchsia::modular::ViewConnection view_connection,
                               fuchsia::modular::SurfaceInfo2 surface_info) {
    if (!view_connection.view_holder_token.value) {
      FXL_LOG(WARNING) << "The module ViewHolder token is not valid, so it "
                          "can't be sent to the story shell.";
      return;
    }

    const auto surface_id = ModulePathToSurfaceID(module_data_.module_path());

    story_controller_impl_->story_shell_->AddSurface3(std::move(view_connection),
                                                      std::move(surface_info));

    story_controller_impl_->connected_views_.emplace(surface_id);
    story_controller_impl_->ProcessPendingStoryShellViews();
    story_controller_impl_->story_shell_->FocusSurface(surface_id);
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::modular::ModuleData module_data_;
  fidl::InterfaceRequest<fuchsia::modular::ModuleController> module_controller_request_;

  fuchsia::modular::ModuleControllerPtr module_controller_;
  fuchsia::modular::ModuleManifestPtr module_manifest_;

  OperationQueue operation_queue_;
};

class StoryControllerImpl::StopCall : public Operation<> {
 public:
  StopCall(StoryControllerImpl* const story_controller_impl, const bool bulk,
           fit::function<void()> done)
      : Operation("StoryControllerImpl::StopCall", std::move(done)),
        story_controller_impl_(story_controller_impl),
        bulk_(bulk) {}

 private:
  void Run() override {
    if (!story_controller_impl_->IsRunning()) {
      Done();
      return;
    }

    story_controller_impl_->SetRuntimeState(fuchsia::modular::StoryState::STOPPING);

    // If this StopCall is part of a bulk operation of story provider that stops
    // all stories at once, no DetachView() notification is given to the session
    // shell.
    if (bulk_) {
      StopStory();
      return;
    }

    // Invocation of DetachView() follows below.
    //
    // The following callback is scheduled twice, once as response from
    // DetachView(), and again as a timeout.
    //
    // The weak pointer is needed because the method invocation would not be
    // cancelled when the OperationQueue holding this Operation instance is
    // deleted, because the method is invoked on an instance outside of the
    // instance that owns the OperationQueue that holds this Operation instance.
    //
    // The argument from_timeout informs whether the invocation was from the
    // timeout or from the method callback. It's used only to log diagnostics.
    fit::callback<void(const bool)> cont =
        [this, weak_this = GetWeakPtr(),
         story_id = story_controller_impl_->story_id_](const bool from_timeout) {
          if (from_timeout) {
            FXL_LOG(INFO) << "DetachView() timed out: story_id=" << story_id;
          }

          if (weak_this) {
            StopStory();
          }
        };

    // We need to attach the callback to both DetachView() and to the timeout
    // (PostDelayedTask(), below). |fit::function| is move-only, not copyable,
    // but we can use the share() method to get a reference-counted copy.
    // Note the fit::function will not be destructed until all callers have
    // released their reference, so don't pass a |FlowToken| to the callback,
    // or it might keep the |Operation| alive longer than you might expect.
    story_controller_impl_->DetachView([cont = cont.share()]() mutable {
      if (cont)
        cont(false);
    });

    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [cont = std::move(cont)]() mutable {
          if (cont)
            cont(true);
        },
        kBasicTimeout);
  }

  void StopStory() {
    std::vector<FuturePtr<>> did_teardowns;
    did_teardowns.reserve(story_controller_impl_->running_mod_infos_.size());

    // Tear down all connections with a fuchsia::modular::ModuleController
    for (auto& running_mod_info : story_controller_impl_->running_mod_infos_) {
      auto did_teardown = Future<>::Create("StoryControllerImpl.StopCall.Run.did_teardown");
      running_mod_info.module_controller_impl->Teardown(did_teardown->Completer());
      did_teardowns.emplace_back(did_teardown);
    }

    Wait("StoryControllerImpl.StopCall.Run.Wait", did_teardowns)
        ->AsyncMap([this] {
          auto did_teardown = Future<>::Create("StoryControllerImpl.StopCall.Run.did_teardown2");
          // If StopCall runs on a story that's not running, there is no story
          // shell.
          if (story_controller_impl_->story_shell_holder_) {
            story_controller_impl_->story_shell_holder_->Teardown(kBasicTimeout,
                                                                  did_teardown->Completer());
          } else {
            did_teardown->Complete();
          }

          return did_teardown;
        })
        ->AsyncMap([this] {
          story_controller_impl_->story_shell_holder_.reset();
          story_controller_impl_->story_shell_.Unbind();

          // Ensure every story storage operation has completed.
          return story_controller_impl_->story_storage_->Sync();
        })
        ->Then([this] {
          story_controller_impl_->SetRuntimeState(fuchsia::modular::StoryState::STOPPED);

          Done();
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned

  // Whether this Stop operation is part of stopping all stories at once. In
  // that case, DetachView() is not called.
  const bool bulk_;
};

class StoryControllerImpl::StopModuleCall : public Operation<> {
 public:
  StopModuleCall(StoryStorage* const story_storage, std::vector<std::string> module_path,
                 fit::function<void()> done)
      : Operation("StoryControllerImpl::StopModuleCall", std::move(done)),
        story_storage_(story_storage),
        module_path_(std::move(module_path)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    // Mark this module as stopped, which is a global state shared between
    // machines to track when the module is explicitly stopped. The module will
    // stop when ledger notifies us back about the module state change,
    // see OnModuleDataUpdated().
    story_storage_->UpdateModuleData(module_path_,
                                     [flow](fuchsia::modular::ModuleDataPtr* module_data_ptr) {
                                       FXL_DCHECK(*module_data_ptr);
                                       (*module_data_ptr)->set_module_deleted(true);
                                     });
  }

  StoryStorage* const story_storage_;  // not owned
  const std::vector<std::string> module_path_;
};

class StoryControllerImpl::StopModuleAndStoryIfEmptyCall : public Operation<> {
 public:
  StopModuleAndStoryIfEmptyCall(StoryControllerImpl* const story_controller_impl,
                                std::vector<std::string> module_path, fit::function<void()> done)
      : Operation("StoryControllerImpl::StopModuleAndStoryIfEmptyCall", std::move(done)),
        story_controller_impl_(story_controller_impl),
        module_path_(std::move(module_path)) {}

 private:
  void Run() override {
    FlowToken flow{this};
    // If this is the last module in the story, stop the whole story instead
    // (which will cause this mod to be stopped also).
    auto* const running_mod_info = story_controller_impl_->FindRunningModInfo(module_path_);
    if (running_mod_info && story_controller_impl_->running_mod_infos_.size() == 1) {
      operation_queue_.Add(
          std::make_unique<StopCall>(story_controller_impl_, false /* bulk */, [flow] {}));
    } else {
      // Otherwise, stop this one module.
      operation_queue_.Add(std::make_unique<StopModuleCall>(story_controller_impl_->story_storage_,
                                                            module_path_, [flow] {}));
    }
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const std::vector<std::string> module_path_;

  OperationQueue operation_queue_;
};

class StoryControllerImpl::OnModuleDataUpdatedCall : public Operation<> {
 public:
  OnModuleDataUpdatedCall(StoryControllerImpl* const story_controller_impl,
                          fuchsia::modular::ModuleData module_data)
      : Operation("StoryControllerImpl::LedgerNotificationCall", [] {}),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)) {}

 private:
  void Run() override {
    FlowToken flow{this};
    if (!story_controller_impl_->IsRunning()) {
      return;
    }

    // Check for existing module at the given path.
    auto* const running_mod_info =
        story_controller_impl_->FindRunningModInfo(module_data_.module_path());
    if (module_data_.module_deleted()) {
      // If the module is running, kill it.
      if (running_mod_info) {
        running_mod_info->is_deleted_property.Set("True");
        operation_queue_.Add(std::make_unique<KillModuleCall>(story_controller_impl_,
                                                              std::move(module_data_), [flow] {}));
      }
      return;
    }

    // We do not auto-start Modules that were added through ModuleContext on
    // other devices.
    //
    // TODO(thatguy): Revisit this decision. It seems wrong: we do not want to
    // auto-start mods added through ModuleContext.EmbedModule(), because we do
    // not have the necessary capabilities (the ViewHolderToken). Mods added
    // through ModuleContext.AddModuleToStory() can be started automatically,
    // however.
    if (module_data_.module_source() == fuchsia::modular::ModuleSource::INTERNAL) {
      return;
    }

    // We reach this point only if we want to start or update an existing
    // external module.
    operation_queue_.Add(std::make_unique<LaunchModuleInShellCall>(
        story_controller_impl_, std::move(module_data_), nullptr /* module_controller_request */,
        [flow] {}));
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::modular::ModuleData module_data_;
};

class StoryControllerImpl::FocusCall : public Operation<> {
 public:
  FocusCall(StoryControllerImpl* const story_controller_impl, std::vector<std::string> module_path)
      : Operation("StoryControllerImpl::FocusCall", [] {}),
        story_controller_impl_(story_controller_impl),
        module_path_(std::move(module_path)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    if (!story_controller_impl_->story_shell_) {
      return;
    }

    story_controller_impl_->story_shell_->FocusSurface(ModulePathToSurfaceID(module_path_));
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  const std::vector<std::string> module_path_;
};

class StoryControllerImpl::DefocusCall : public Operation<> {
 public:
  DefocusCall(StoryControllerImpl* const story_controller_impl,
              std::vector<std::string> module_path)
      : Operation("StoryControllerImpl::DefocusCall", [] {}),
        story_controller_impl_(story_controller_impl),
        module_path_(std::move(module_path)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    if (!story_controller_impl_->story_shell_) {
      return;
    }

    // NOTE(mesch): We don't wait for defocus to return. TODO(mesch): What is
    // the return callback good for anyway?
    story_controller_impl_->story_shell_->DefocusSurface(ModulePathToSurfaceID(module_path_),
                                                         [] {});
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  const std::vector<std::string> module_path_;
};

// An operation that first performs module resolution with the provided
// fuchsia::modular::Intent and subsequently starts the most appropriate
// resolved module in the story shell.
class StoryControllerImpl::AddIntentCall : public Operation<fuchsia::modular::StartModuleStatus> {
 public:
  AddIntentCall(
      StoryControllerImpl* const story_controller_impl, AddModParams add_mod_params,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController> module_controller_request,
      std::optional<fuchsia::ui::views::ViewToken> view_token, ResultCall result_call)
      : Operation("StoryControllerImpl::AddIntentCall", std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        add_mod_params_(std::move(add_mod_params)),
        view_token_(std::move(view_token)),
        module_controller_request_(std::move(module_controller_request)) {}

 private:
  void Run() override {
    FlowToken flow{this, &start_module_status_};
    AddAddModOperation(
        &operation_queue_, story_controller_impl_->story_storage_,
        story_controller_impl_->story_provider_impl_->module_resolver(),
        story_controller_impl_->story_provider_impl_->entity_resolver(), std::move(add_mod_params_),
        [this, flow](fuchsia::modular::ExecuteResult result,
                     fuchsia::modular::ModuleData module_data) {
          if (result.status == fuchsia::modular::ExecuteStatus::NO_MODULES_FOUND) {
            start_module_status_ = fuchsia::modular::StartModuleStatus::NO_MODULES_FOUND;
            return;
          }
          if (result.status != fuchsia::modular::ExecuteStatus::OK) {
            FXL_LOG(WARNING) << "StoryController::AddIntentCall::AddModCall returned "
                             << "error response with message: " << result.error_message;
          }
          module_data_ = std::move(module_data);
          LaunchModuleIfStoryRunning(flow);
        });
  }

  void LaunchModuleIfStoryRunning(FlowToken flow) {
    if (story_controller_impl_->IsRunning()) {
      // TODO(thatguy): Should we be checking surface_relation also?
      if (!view_token_) {
        operation_queue_.Add(std::make_unique<LaunchModuleInShellCall>(
            story_controller_impl_, std::move(module_data_), std::move(module_controller_request_),
            [flow] {}));
      } else {
        operation_queue_.Add(std::make_unique<LaunchModuleCall>(
            story_controller_impl_, std::move(module_data_), std::move(module_controller_request_),
            std::move(view_token_), [this, flow] {
              // LaunchModuleInShellCall above already calls
              // ProcessPendingStoryShellViews(). NOTE(thatguy): This
              // cannot be moved into LaunchModuleCall, because
              // LaunchModuleInShellCall uses LaunchModuleCall
              // as the very first step of its operation. This
              // would inform the story shell of a new module
              // before we had told it about its
              // surface-relation parent (which we do as the
              // second part of LaunchModuleInShellCall).  So
              // we must defer to here.
              story_controller_impl_->ProcessPendingStoryShellViews();
            }));
      }
    }

    start_module_status_ = fuchsia::modular::StartModuleStatus::SUCCESS;
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;  // not owned

  // Some of the fields in add_mod_params_ are used to initialize module_data_
  // in AddModuleFromResult().
  AddModParams add_mod_params_;
  std::optional<fuchsia::ui::views::ViewToken> view_token_;
  fidl::InterfaceRequest<fuchsia::modular::ModuleController> module_controller_request_;

  // Created by AddModuleFromResult, and ultimately written to story state.
  fuchsia::modular::ModuleData module_data_;

  fuchsia::modular::StartModuleStatus start_module_status_{
      fuchsia::modular::StartModuleStatus::NO_MODULES_FOUND};
};

class StoryControllerImpl::StartCall : public Operation<> {
 public:
  StartCall(StoryControllerImpl* const story_controller_impl, StoryStorage* const storage)
      : Operation("StoryControllerImpl::StartCall", [] {}),
        story_controller_impl_(story_controller_impl),
        storage_(storage) {}

 private:
  void Run() override {
    FlowToken flow{this};

    // If the story is running, we do nothing.
    if (story_controller_impl_->IsRunning()) {
      FXL_LOG(INFO) << "StoryControllerImpl::StartCall() while already running: ignored.";
      return;
    }

    story_controller_impl_->StartStoryShell();

    // Start all modules that were not themselves explicitly started by another
    // module.
    storage_->ReadAllModuleData()->Then(
        [this, flow](std::vector<fuchsia::modular::ModuleData> data) {
          for (auto& module_data : data) {
            // Don't start the module if it is embedded, or if it has been
            // marked deleted.
            if (module_data.module_deleted() || module_data.is_embedded()) {
              continue;
            }
            FXL_CHECK(module_data.has_intent());
            operation_queue_.Add(std::make_unique<LaunchModuleInShellCall>(
                story_controller_impl_, std::move(module_data),
                nullptr /* module_controller_request */, [flow] {}));
          }

          story_controller_impl_->SetRuntimeState(fuchsia::modular::StoryState::RUNNING);
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  StoryStorage* const storage_;                       // not owned

  OperationQueue operation_queue_;
};

StoryControllerImpl::StoryControllerImpl(SessionStorage* const session_storage,
                                         StoryStorage* const story_storage,
                                         std::unique_ptr<StoryMutator> story_mutator,
                                         std::unique_ptr<StoryObserver> story_observer,
                                         StoryProviderImpl* const story_provider_impl,
                                         inspect::Node* story_inspect_node)
    : story_id_(story_observer->model().name()),
      story_provider_impl_(story_provider_impl),
      session_storage_(session_storage),
      story_storage_(story_storage),
      story_inspect_node_(story_inspect_node),
      story_mutator_(std::move(story_mutator)),
      story_observer_(std::move(story_observer)),
      story_shell_context_impl_{story_id_, story_provider_impl},
      weak_factory_(this) {
  story_storage_->set_on_module_data_updated([this](fuchsia::modular::ModuleData module_data) {
    auto* const running_mod_info = FindRunningModInfo(module_data.module_path());
    if (running_mod_info) {
      if (module_data.has_annotations()) {
        fidl::Clone(module_data.annotations(),
                    running_mod_info->module_data->mutable_annotations());
      }
      running_mod_info->ResetInspect();
    }
    OnModuleDataUpdated(std::move(module_data));
  });

  story_observer_->RegisterListener([this](const fuchsia::modular::storymodel::StoryModel& model) {
    NotifyStoryWatchers(model);
  });
}

StoryControllerImpl::~StoryControllerImpl() = default;

void StoryControllerImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::StoryController> request) {
  bindings_.AddBinding(this, std::move(request));
}

bool StoryControllerImpl::IsRunning() {
  switch (story_observer_->model().runtime_state()) {
    case fuchsia::modular::StoryState::RUNNING:
      return true;
    case fuchsia::modular::StoryState::STOPPING:
    case fuchsia::modular::StoryState::STOPPED:
      return false;
  }
}

void StoryControllerImpl::Sync(fit::function<void()> done) {
  operation_queue_.Add(std::make_unique<SyncCall>(std::move(done)));
}

void StoryControllerImpl::FocusModule(const std::vector<std::string>& module_path) {
  operation_queue_.Add(std::make_unique<FocusCall>(this, module_path));
}

void StoryControllerImpl::DefocusModule(const std::vector<std::string>& module_path) {
  operation_queue_.Add(std::make_unique<DefocusCall>(this, module_path));
}

void StoryControllerImpl::StopModule(const std::vector<std::string>& module_path,
                                     fit::function<void()> done) {
  operation_queue_.Add(
      std::make_unique<StopModuleCall>(story_storage_, module_path, std::move(done)));
}

void StoryControllerImpl::ReleaseModule(ModuleControllerImpl* const module_controller_impl) {
  auto fit = std::find_if(running_mod_infos_.begin(), running_mod_infos_.end(),
                          [module_controller_impl](const RunningModInfo& c) {
                            return c.module_controller_impl.get() == module_controller_impl;
                          });
  FXL_DCHECK(fit != running_mod_infos_.end());
  fit->module_controller_impl.release();
  pending_story_shell_views_.erase(ModulePathToSurfaceID(fit->module_data->module_path()));
  running_mod_infos_.erase(fit);
}

fidl::StringPtr StoryControllerImpl::GetStoryId() const { return story_observer_->model().name(); }

void StoryControllerImpl::EmbedModule(
    AddModParams add_mod_params,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController> module_controller_request,
    fuchsia::ui::views::ViewToken view_token,
    fit::function<void(fuchsia::modular::StartModuleStatus)> callback) {
  operation_queue_.Add(std::make_unique<AddIntentCall>(this, std::move(add_mod_params),
                                                       std::move(module_controller_request),
                                                       std::move(view_token), std::move(callback)));
}

void StoryControllerImpl::AddModuleToStory(
    AddModParams add_mod_params,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController> module_controller_request,
    fit::function<void(fuchsia::modular::StartModuleStatus)> callback) {
  operation_queue_.Add(std::make_unique<AddIntentCall>(
      this, std::move(add_mod_params), std::move(module_controller_request),
      /*view_token=*/std::nullopt, std::move(callback)));
}

void StoryControllerImpl::ProcessPendingStoryShellViews() {
  // NOTE(mesch): As it stands, this machinery to send modules in traversal
  // order to the story shell is N^3 over the lifetime of the story, where N
  // is the number of modules. This function is N^2, and it's called once for
  // each of the N modules. However, N is small, and moreover its scale is
  // limited my much more severe constraints. Eventually, we will address this
  // by changing story shell to be able to accomodate modules out of traversal
  // order.
  if (!story_shell_) {
    return;
  }

  std::vector<fidl::StringPtr> added_keys;

  for (auto& kv : pending_story_shell_views_) {
    auto* const running_mod_info = FindRunningModInfo(kv.second.module_path);
    if (!running_mod_info) {
      continue;
    }

    auto* const anchor = FindAnchor(running_mod_info);
    if (!anchor) {
      continue;
    }

    const auto anchor_surface_id = ModulePathToSurfaceID(anchor->module_data->module_path());
    if (!connected_views_.count(anchor_surface_id)) {
      continue;
    }

    if (!kv.second.view_connection.view_holder_token.value) {
      FXL_LOG(WARNING) << "The module ViewHolder token is not valid, so it "
                          "can't be sent to the story shell.";
      // Erase the pending view in this case, as it will never become valid.
      added_keys.push_back(kv.first);
      continue;
    }

    const auto surface_id = ModulePathToSurfaceID(kv.second.module_path);

    story_shell_->AddSurface3(std::move(kv.second.view_connection),
                              std::move(kv.second.surface_info));
    connected_views_.emplace(surface_id);

    added_keys.push_back(kv.first);
  }

  if (added_keys.size()) {
    for (auto& key : added_keys) {
      pending_story_shell_views_.erase(key.value_or(""));
    }
    ProcessPendingStoryShellViews();
  }
}

void StoryControllerImpl::OnModuleDataUpdated(fuchsia::modular::ModuleData module_data) {
  operation_queue_.Add(std::make_unique<OnModuleDataUpdatedCall>(this, std::move(module_data)));
}

void StoryControllerImpl::GetInfo(GetInfoCallback callback) {
  // Synced such that if GetInfo() is called after Start() or Stop(), the
  // state after the previously invoked operation is returned.
  //
  // If this call enters a race with a StoryProvider.DeleteStory() call,
  // resulting in |this| being destroyed, |callback| will be dropped.
  operation_queue_.Add(std::make_unique<SyncCall>([this, callback = std::move(callback)] {
    auto story_info_2 = story_provider_impl_->GetCachedStoryInfo(story_id_);
    FXL_CHECK(story_info_2);
    auto story_info = modular::StoryProviderImpl::StoryInfo2ToStoryInfo(*story_info_2);
    callback(std::move(story_info), story_observer_->model().runtime_state());
  }));
}

void StoryControllerImpl::GetInfo2(GetInfo2Callback callback) {
  // Synced such that if GetInfo() is called after Start() or Stop(), the
  // state after the previously invoked operation is returned.
  //
  // If this call enters a race with a StoryProvider.DeleteStory() call,
  // resulting in |this| being destroyed, |callback| will be dropped.
  operation_queue_.Add(std::make_unique<SyncCall>([this, callback = std::move(callback)] {
    auto story_info_2 = story_provider_impl_->GetCachedStoryInfo(story_id_);
    FXL_CHECK(story_info_2);
    callback(std::move(*story_info_2), story_observer_->model().runtime_state());
  }));
}

void StoryControllerImpl::RequestStart() {
  operation_queue_.Add(std::make_unique<StartCall>(this, story_storage_));
}

void StoryControllerImpl::Stop(StopCallback done) {
  operation_queue_.Add(std::make_unique<StopCall>(this, false /* bulk */, std::move(done)));
}

void StoryControllerImpl::StopBulk(const bool bulk, StopCallback done) {
  operation_queue_.Add(std::make_unique<StopCall>(this, bulk, std::move(done)));
}

void StoryControllerImpl::Watch(fidl::InterfaceHandle<fuchsia::modular::StoryWatcher> watcher) {
  auto ptr = watcher.Bind();
  NotifyOneStoryWatcher(story_observer_->model(), ptr.get());
  watchers_.AddInterfacePtr(std::move(ptr));
}

void StoryControllerImpl::StartStoryShell() {
  auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

  story_shell_holder_ = story_provider_impl_->StartStoryShell(story_id_, std::move(view_token),
                                                              story_shell_.NewRequest());

  story_provider_impl_->AttachView(story_id_, std::move(view_holder_token));

  fuchsia::modular::StoryShellContextPtr story_shell_context;
  story_shell_context_impl_.Connect(story_shell_context.NewRequest());
  story_shell_->Initialize(std::move(story_shell_context));
  story_shell_.events().OnSurfaceFocused =
      fit::bind_member(this, &StoryControllerImpl::OnSurfaceFocused);
}

void StoryControllerImpl::DetachView(fit::function<void()> done) {
  story_provider_impl_->DetachView(story_id_, std::move(done));
}

void StoryControllerImpl::SetRuntimeState(const fuchsia::modular::StoryState new_state) {
  story_mutator_->set_runtime_state(new_state);
}

void StoryControllerImpl::NotifyStoryWatchers(
    const fuchsia::modular::storymodel::StoryModel& model) {
  for (auto& i : watchers_.ptrs()) {
    NotifyOneStoryWatcher(model, (*i).get());
  }
}

void StoryControllerImpl::NotifyOneStoryWatcher(
    const fuchsia::modular::storymodel::StoryModel& model,
    fuchsia::modular::StoryWatcher* watcher) {
  watcher->OnStateChange(model.runtime_state());
}

bool StoryControllerImpl::IsExternalModule(const std::vector<std::string>& module_path) {
  auto* const i = FindRunningModInfo(module_path);
  if (!i) {
    return false;
  }

  return i->module_data->module_source() == fuchsia::modular::ModuleSource::EXTERNAL;
}

StoryControllerImpl::RunningModInfo* StoryControllerImpl::FindRunningModInfo(
    const std::vector<std::string>& module_path) {
  for (auto& c : running_mod_infos_) {
    if (c.module_data->module_path() == module_path) {
      return &c;
    }
  }
  return nullptr;
}

StoryControllerImpl::RunningModInfo* StoryControllerImpl::FindAnchor(
    RunningModInfo* running_mod_info) {
  if (!running_mod_info) {
    return nullptr;
  }

  auto* anchor = FindRunningModInfo(ParentModulePath(running_mod_info->module_data->module_path()));

  // Traverse up until there is a non-embedded module.
  while (anchor && anchor->module_data->is_embedded()) {
    anchor = FindRunningModInfo(ParentModulePath(anchor->module_data->module_path()));
  }

  return anchor;
}

void StoryControllerImpl::RemoveModuleFromStory(const std::vector<std::string>& module_path) {
  operation_queue_.Add(std::make_unique<StopModuleAndStoryIfEmptyCall>(this, module_path, [] {}));
}

void StoryControllerImpl::CreateEntity(
    std::string type, fuchsia::mem::Buffer data,
    fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request,
    fit::function<void(std::string /* entity_reference */)> callback) {
  story_provider_impl_->CreateEntity(story_id_, type, std::move(data), std::move(entity_request),
                                     std::move(callback));
}

void StoryControllerImpl::OnSurfaceFocused(fidl::StringPtr surface_id) {
  auto module_path = ModulePathFromSurfaceID(surface_id.value_or(""));

  for (auto& watcher : watchers_.ptrs()) {
    (*watcher)->OnModuleFocused(std::move(module_path));
  }
}

void StoryControllerImpl::Annotate(std::vector<fuchsia::modular::Annotation> annotations,
                                   AnnotateCallback callback) {
  operation_queue_.Add(std::make_unique<SyncCall>([weak_this = weak_factory_.GetWeakPtr(),
                                                   annotations = std::move(annotations),
                                                   callback = std::move(callback)]() mutable {
    if (!weak_this) {
      fuchsia::modular::StoryController_Annotate_Result result{};
      result.set_err(fuchsia::modular::AnnotationError::NOT_FOUND);
      callback(std::move(result));
      return;
    }
    for (auto const& annotation : annotations) {
      if (annotation.value && annotation.value->is_buffer() &&
          annotation.value->buffer().size >
              fuchsia::modular::MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES) {
        fuchsia::modular::StoryController_Annotate_Result result{};
        result.set_err(fuchsia::modular::AnnotationError::VALUE_TOO_BIG);
        callback(std::move(result));
        return;
      }
    }
    weak_this->session_storage_->MergeStoryAnnotations(weak_this->story_id_, std::move(annotations))
        ->WeakThen(weak_this, [callback = std::move(callback)](
                                  std::optional<fuchsia::modular::AnnotationError> error) {
          fuchsia::modular::StoryController_Annotate_Result result{};
          if (error.has_value()) {
            result.set_err(error.value());
          } else {
            result.set_response({});
          }
          callback(std::move(result));
        });
  }));
}

}  // namespace modular
