// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story_runner/story_controller_impl.h"

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/storymodel/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/future.h>
#include <lib/async/default.h>
#include <lib/component/cpp/connect.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/entity/cpp/json.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fsl/types/type_converters.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/join_strings.h>
#include <src/lib/fxl/strings/split_string.h>

#include "peridot/bin/basemgr/cobalt/cobalt.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/add_mod_call.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/find_modules_call.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/get_types_from_entity_call.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/initialize_chain_call.h"
#include "peridot/bin/sessionmgr/storage/constants_and_utils.h"
#include "peridot/bin/sessionmgr/storage/story_storage.h"
#include "peridot/bin/sessionmgr/story/model/story_observer.h"
#include "peridot/bin/sessionmgr/story_runner/link_impl.h"
#include "peridot/bin/sessionmgr/story_runner/module_context_impl.h"
#include "peridot/bin/sessionmgr/story_runner/module_controller_impl.h"
#include "peridot/bin/sessionmgr/story_runner/ongoing_activity_impl.h"
#include "peridot/bin/sessionmgr/story_runner/story_provider_impl.h"
#include "peridot/bin/sessionmgr/story_runner/story_shell_context_impl.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/util/string_escape.h"

// Used to create std::set<LinkPath>.
namespace std {
template <>
struct less<fuchsia::modular::LinkPath> {
  bool operator()(const fuchsia::modular::LinkPath& left,
                  const fuchsia::modular::LinkPath& right) const {
    if (left.module_path == right.module_path) {
      return left.link_name < right.link_name;
    }
    return left.module_path < right.module_path;
  }
};
}  // namespace std

namespace modular {

constexpr char kStoryEnvironmentLabelPrefix[] = "story-";
constexpr auto kUpdateSnapshotTimeout = zx::sec(1);

namespace {

constexpr char kSurfaceIDSeparator[] = ":";
fidl::StringPtr ModulePathToSurfaceID(
    const std::vector<std::string>& module_path) {
  std::vector<std::string> path;
  // Sanitize all the |module_name|s that make up this |module_path|.
  for (const auto& module_name : module_path) {
    path.push_back(StringEscape(module_name, kSurfaceIDSeparator));
  }
  return fxl::JoinStrings(path, kSurfaceIDSeparator);
}

std::vector<std::string> ModulePathFromSurfaceID(
    const std::string& surface_id) {
  std::vector<std::string> path;
  for (const auto& parts : SplitEscapedString(fxl::StringView(surface_id),
                                              kSurfaceIDSeparator[0])) {
    path.push_back(parts.ToString());
  }
  return path;
}

std::vector<std::string> ParentModulePath(
    const std::vector<std::string>& module_path) {
  std::vector<std::string> ret;

  if (module_path.size() > 0) {
    for (size_t i = 0; i < module_path.size() - 1; i++) {
      ret.push_back(module_path.at(i));
    }
  }
  return ret;
}

}  // namespace

bool ShouldRestartModuleForNewIntent(
    const fuchsia::modular::Intent& old_intent,
    const fuchsia::modular::Intent& new_intent) {
  if (old_intent.handler != new_intent.handler) {
    return true;
  }

  return false;
}

zx_time_t GetNowUTC() {
  zx_time_t now = 0u;
  zx_clock_get_new(ZX_CLOCK_UTC, &now);
  return now;
}

// Launches (brings up a running instance) of a module.
//
// If the module is to be composed into the story shell, notifies the story
// shell of the new module. If the module is composed internally, connects the
// view owner request appropriately.
class StoryControllerImpl::LaunchModuleCall : public Operation<> {
 public:
  LaunchModuleCall(StoryControllerImpl* const story_controller_impl,
                   fuchsia::modular::ModuleData module_data,
                   fidl::InterfaceRequest<fuchsia::modular::ModuleController>
                       module_controller_request,
                   fuchsia::ui::views::ViewToken view_token,
                   ResultCall result_call)
      : Operation("StoryControllerImpl::LaunchModuleCall",
                  std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)),
        view_token_(std::move(view_token)),
        module_controller_request_(std::move(module_controller_request)),
        start_time_(GetNowUTC()) {}

 private:
  void Run() override {
    FlowToken flow{this};

    RunningModInfo* const running_mod_info =
        story_controller_impl_->FindRunningModInfo(module_data_.module_path);

    // We launch the new module if it doesn't run yet.
    if (!running_mod_info) {
      Launch(flow);
      return;
    }

    // If the new module is already running, but with a different Intent, we
    // tear it down then launch a new instance.
    if (ShouldRestartModuleForNewIntent(*running_mod_info->module_data->intent,
                                        *module_data_.intent)) {
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
      running_mod_info->module_controller_impl->Connect(
          std::move(module_controller_request_));
    }

    // Since the module is already running send it the new intent.
    NotifyModuleOfIntent(*running_mod_info);
  }

  void Launch(FlowToken /*flow*/) {
    FXL_LOG(INFO) << "StoryControllerImpl::LaunchModule() "
                  << module_data_.module_url << " "
                  << ModulePathToSurfaceID(module_data_.module_path);
    fuchsia::modular::AppConfig module_config;
    module_config.url = module_data_.module_url;

    fuchsia::sys::ServiceProviderPtr module_context_provider;
    auto module_context_provider_request = module_context_provider.NewRequest();
    auto service_list = fuchsia::sys::ServiceList::New();
    service_list->names.push_back(fuchsia::modular::ComponentContext::Name_);
    service_list->names.push_back(fuchsia::modular::ModuleContext::Name_);
    service_list->names.push_back(
        fuchsia::modular::IntelligenceServices::Name_);
    service_list->provider = std::move(module_context_provider);

    RunningModInfo running_mod_info;
    running_mod_info.module_data = CloneOptional(module_data_);

    // ModuleControllerImpl's constructor launches the child application.
    running_mod_info.module_controller_impl =
        std::make_unique<ModuleControllerImpl>(
            story_controller_impl_,
            story_controller_impl_->story_environment_->GetLauncher(),
            std::move(module_config), running_mod_info.module_data.get(),
            std::move(service_list), std::move(view_token_));

    // Modules added/started through PuppetMaster don't have a module
    // controller request.
    if (module_controller_request_.is_valid()) {
      running_mod_info.module_controller_impl->Connect(
          std::move(module_controller_request_));
    }

    ModuleContextInfo module_context_info = {
        story_controller_impl_->story_provider_impl_->component_context_info(),
        story_controller_impl_,
        story_controller_impl_->story_visibility_system_,
        story_controller_impl_->story_provider_impl_
            ->user_intelligence_provider()};

    running_mod_info.module_context_impl = std::make_unique<ModuleContextImpl>(
        module_context_info, running_mod_info.module_data.get(),
        std::move(module_context_provider_request));

    NotifyModuleOfIntent(running_mod_info);

    story_controller_impl_->running_mod_infos_.emplace_back(
        std::move(running_mod_info));

    for (auto& i : story_controller_impl_->watchers_.ptrs()) {
      fuchsia::modular::ModuleData module_data;
      module_data_.Clone(&module_data);
      (*i)->OnModuleAdded(std::move(module_data));
    }

    zx_time_t now = 0;
    zx_clock_get_new(ZX_CLOCK_UTC, &now);
    ReportModuleLaunchTime(module_data_.module_url,
                           zx::duration(now - start_time_));
  }

  // Connects to the module's intent handler and sends it the intent from
  // |module_data_.intent|.
  void NotifyModuleOfIntent(const RunningModInfo& running_mod_info) {
    if (!module_data_.intent) {
      return;
    }
    fuchsia::modular::IntentHandlerPtr intent_handler;
    running_mod_info.module_controller_impl->services().ConnectToService(
        intent_handler.NewRequest());
    fuchsia::modular::Intent intent;
    module_data_.intent->Clone(&intent);

    intent_handler->HandleIntent(std::move(intent));
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::modular::ModuleData module_data_;
  fuchsia::ui::views::ViewToken view_token_;
  fidl::InterfaceRequest<fuchsia::modular::ModuleController>
      module_controller_request_;
  const zx_time_t start_time_;
};

// KillModuleCall tears down the module by the given module_data. It is enqueued
// when ledger confirms that the module was stopped, see OnModuleDataUpdated().
class StoryControllerImpl::KillModuleCall : public Operation<> {
 public:
  KillModuleCall(StoryControllerImpl* const story_controller_impl,
                 fuchsia::modular::ModuleData module_data,
                 fit::function<void()> done)
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
    auto future =
        Future<>::Create("StoryControllerImpl.KillModuleCall.Run.future");
    if (story_controller_impl_->story_shell_ &&
        module_data_.module_source ==
            fuchsia::modular::ModuleSource::EXTERNAL) {
      story_controller_impl_->story_shell_->DefocusSurface(
          ModulePathToSurfaceID(module_data_.module_path), future->Completer());
    } else {
      future->Complete();
    }

    future->Then([this, flow] {
      // Teardown the module, which discards the module controller. Since
      // multiple KillModuleCall operations can be queued by module data
      // updates, we must check whether the module has already been killed.
      auto* const running_mod_info =
          story_controller_impl_->FindRunningModInfo(module_data_.module_path);
      if (!running_mod_info) {
        FXL_LOG(INFO) << "No ModuleController for Module '"
                      << ModulePathToSurfaceID(module_data_.module_path)
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
      running_mod_info->module_controller_impl->Teardown(
          [this, flow] { InvokeDone(); });
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
      StoryControllerImpl* const story_controller_impl,
      fuchsia::modular::ModuleData module_data,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController>
          module_controller_request,
      ResultCall result_call)
      : Operation("StoryControllerImpl::LaunchModuleInShellCall",
                  std::move(result_call), module_data.module_url),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)),
        module_controller_request_(std::move(module_controller_request)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    // TODO(mesch): The LaunchModuleCall may result in just a new
    // fuchsia::modular::ModuleController connection to an existing
    // ModuleControllerImpl. In that case, the view owner request is
    // closed, and the view owner should not be sent to the story
    // shell.
    operation_queue_.Add(std::make_unique<LaunchModuleCall>(
        story_controller_impl_, fidl::Clone(module_data_),
        std::move(module_controller_request_),
        scenic::ToViewToken(
            zx::eventpair(view_owner_.NewRequest().TakeChannel().release())),
        [this, flow] { LoadModuleManifest(flow); }));
    view_owner_.set_error_handler([module_url =
                                       module_data_.module_url](zx_status_t) {
      FXL_LOG(ERROR) << "ViewOwner associated with module_url=" << module_url
                     << " died. This module likely won't be able to display "
                        "anything on the screen.";
    });
  }

  void LoadModuleManifest(FlowToken flow) {
    story_controller_impl_->story_provider_impl_->module_facet_reader()
        ->GetModuleManifest(
            module_data_.module_url,
            [this, flow](fuchsia::modular::ModuleManifestPtr manifest) {
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

    if (module_data_.module_path.size() == 1) {
      // This is a root module; pass it's view on to the story shell.
      ConnectViewToStoryShell(flow, "");
      return;
    }

    auto* const running_mod_info =
        story_controller_impl_->FindRunningModInfo(module_data_.module_path);
    FXL_CHECK(running_mod_info);  // This was just created in LaunchModuleCall.

    auto* const anchor = story_controller_impl_->FindAnchor(running_mod_info);
    if (anchor) {
      const auto anchor_surface_id =
          ModulePathToSurfaceID(anchor->module_data->module_path);
      if (story_controller_impl_->connected_views_.count(anchor_surface_id)) {
        ConnectViewToStoryShell(flow, anchor_surface_id);
        return;
      }
    }

    auto manifest_clone = fuchsia::modular::ModuleManifest::New();
    fidl::Clone(module_manifest_, &manifest_clone);

    fuchsia::modular::SurfaceRelationPtr surface_relation_clone;
    if (module_data_.surface_relation) {
      surface_relation_clone = fuchsia::modular::SurfaceRelation::New();
      module_data_.surface_relation->Clone(surface_relation_clone.get());
    }

    story_controller_impl_->pending_story_shell_views_.emplace(
        ModulePathToSurfaceID(module_data_.module_path),
        PendingViewForStoryShell{
            module_data_.module_path, std::move(manifest_clone),
            std::move(surface_relation_clone), module_data_.module_source,
            std::move(view_owner_)});
  }

  void ConnectViewToStoryShell(FlowToken flow,
                               fidl::StringPtr anchor_surface_id) {
    if (!view_owner_.is_bound()) {
      FXL_LOG(WARNING)
          << "The module view owner connection is not bound, so it "
             "can't be sent to the story shell.";
      return;
    }

    const auto surface_id = ModulePathToSurfaceID(module_data_.module_path);

    fuchsia::modular::ViewConnection view_connection;
    view_connection.surface_id = surface_id;
    view_connection.owner = std::move(view_owner_);

    fuchsia::modular::SurfaceInfo surface_info;
    surface_info.parent_id = anchor_surface_id;
    surface_info.surface_relation = std::move(module_data_.surface_relation);
    surface_info.module_manifest = std::move(module_manifest_);
    surface_info.module_source = std::move(module_data_.module_source);
    story_controller_impl_->story_shell_->AddSurface(std::move(view_connection),
                                                     std::move(surface_info));

    story_controller_impl_->connected_views_.emplace(surface_id);
    story_controller_impl_->ProcessPendingStoryShellViews();
    story_controller_impl_->story_shell_->FocusSurface(surface_id);
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::modular::ModuleData module_data_;
  fidl::InterfaceRequest<fuchsia::modular::ModuleController>
      module_controller_request_;

  fuchsia::modular::ModuleControllerPtr module_controller_;
  fuchsia::ui::viewsv1token::ViewOwnerPtr view_owner_;

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

    story_controller_impl_->SetRuntimeState(
        fuchsia::modular::StoryState::STOPPING);

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
         story_id =
             story_controller_impl_->story_id_](const bool from_timeout) {
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
    // first, then the links between them.
    for (auto& running_mod_info : story_controller_impl_->running_mod_infos_) {
      auto did_teardown =
          Future<>::Create("StoryControllerImpl.StopCall.Run.did_teardown");
      running_mod_info.module_controller_impl->Teardown(
          did_teardown->Completer());
      did_teardowns.emplace_back(did_teardown);
    }

    Wait("StoryControllerImpl.StopCall.Run.Wait", did_teardowns)
        ->AsyncMap([this] {
          auto did_teardown = Future<>::Create(
              "StoryControllerImpl.StopCall.Run.did_teardown2");
          // If StopCall runs on a story that's not running, there is no story
          // shell.
          if (story_controller_impl_->story_shell_holder_) {
            story_controller_impl_->story_shell_holder_->Teardown(
                kBasicTimeout, did_teardown->Completer());
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
          // Clear the remaining links and connections in case there are some
          // left. At this point, no DisposeLink() calls can arrive anymore.
          story_controller_impl_->link_impls_.CloseAll();

          // There should be no ongoing activities since all the modules have
          // been destroyed at this point.
          FXL_DCHECK(story_controller_impl_->ongoing_activities_.size() == 0);

          story_controller_impl_->SetRuntimeState(
              fuchsia::modular::StoryState::STOPPED);

          story_controller_impl_->DestroyStoryEnvironment();

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
  StopModuleCall(StoryStorage* const story_storage,
                 const std::vector<std::string>& module_path,
                 fit::function<void()> done)
      : Operation("StoryControllerImpl::StopModuleCall", std::move(done)),
        story_storage_(story_storage),
        module_path_(module_path) {}

 private:
  void Run() override {
    FlowToken flow{this};

    // Mark this module as stopped, which is a global state shared between
    // machines to track when the module is explicitly stopped. The module will
    // stop when ledger notifies us back about the module state change,
    // see OnModuleDataUpdated().
    story_storage_->UpdateModuleData(
        module_path_, [flow](fuchsia::modular::ModuleDataPtr* module_data_ptr) {
          FXL_DCHECK(*module_data_ptr);
          (*module_data_ptr)->module_deleted = true;
        });
  }

  StoryStorage* const story_storage_;  // not owned
  const std::vector<std::string> module_path_;
};

class StoryControllerImpl::StopModuleAndStoryIfEmptyCall : public Operation<> {
 public:
  StopModuleAndStoryIfEmptyCall(
      StoryControllerImpl* const story_controller_impl,
      const std::vector<std::string>& module_path, fit::function<void()> done)
      : Operation("StoryControllerImpl::StopModuleAndStoryIfEmptyCall",
                  std::move(done)),
        story_controller_impl_(story_controller_impl),
        module_path_(module_path) {}

 private:
  void Run() override {
    FlowToken flow{this};
    // If this is the last module in the story, stop the whole story instead
    // (which will cause this mod to be stopped also).
    auto* const running_mod_info =
        story_controller_impl_->FindRunningModInfo(module_path_);
    if (running_mod_info &&
        story_controller_impl_->running_mod_infos_.size() == 1) {
      operation_queue_.Add(std::make_unique<StopCall>(
          story_controller_impl_, false /* bulk */, [flow] {}));
    } else {
      // Otherwise, stop this one module.
      operation_queue_.Add(std::make_unique<StopModuleCall>(
          story_controller_impl_->story_storage_, module_path_, [flow] {}));
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
        story_controller_impl_->FindRunningModInfo(module_data_.module_path);
    if (module_data_.module_deleted) {
      // If the module is running, kill it.
      if (running_mod_info) {
        operation_queue_.Add(std::make_unique<KillModuleCall>(
            story_controller_impl_, std::move(module_data_), [flow] {}));
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
    if (module_data_.module_source ==
        fuchsia::modular::ModuleSource::INTERNAL) {
      return;
    }

    // We reach this point only if we want to start or update an existing
    // external module.
    operation_queue_.Add(std::make_unique<LaunchModuleInShellCall>(
        story_controller_impl_, std::move(module_data_),
        nullptr /* module_controller_request */, [flow] {}));
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::modular::ModuleData module_data_;
};

class StoryControllerImpl::FocusCall : public Operation<> {
 public:
  FocusCall(StoryControllerImpl* const story_controller_impl,
            std::vector<std::string> module_path)
      : Operation("StoryControllerImpl::FocusCall", [] {}),
        story_controller_impl_(story_controller_impl),
        module_path_(std::move(module_path)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    if (!story_controller_impl_->story_shell_) {
      return;
    }

    story_controller_impl_->story_shell_->FocusSurface(
        ModulePathToSurfaceID(module_path_));
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
    story_controller_impl_->story_shell_->DefocusSurface(
        ModulePathToSurfaceID(module_path_), [] {});
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  const std::vector<std::string> module_path_;
};

// An operation that first performs module resolution with the provided
// fuchsia::modular::Intent and subsequently starts the most appropriate
// resolved module in the story shell.
class StoryControllerImpl::AddIntentCall
    : public Operation<fuchsia::modular::StartModuleStatus> {
 public:
  AddIntentCall(StoryControllerImpl* const story_controller_impl,
                AddModParams add_mod_params,
                fidl::InterfaceRequest<fuchsia::modular::ModuleController>
                    module_controller_request,
                fuchsia::ui::views::ViewToken view_token,
                ResultCall result_call)
      : Operation("StoryControllerImpl::AddIntentCall", std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        add_mod_params_(std::move(add_mod_params)),
        view_token_(std::move(view_token)),
        module_controller_request_(std::move(module_controller_request)) {}

 private:
  void Run() {
    FlowToken flow{this, &start_module_status_};
    AddAddModOperation(
        &operation_queue_, story_controller_impl_->story_storage_,
        story_controller_impl_->story_provider_impl_->module_resolver(),
        story_controller_impl_->story_provider_impl_->entity_resolver(),
        std::move(add_mod_params_),
        [this, flow](fuchsia::modular::ExecuteResult result,
                     fuchsia::modular::ModuleData module_data) {
          if (result.status ==
              fuchsia::modular::ExecuteStatus::NO_MODULES_FOUND) {
            start_module_status_ =
                fuchsia::modular::StartModuleStatus::NO_MODULES_FOUND;
            return;
          }
          if (result.status != fuchsia::modular::ExecuteStatus::OK) {
            FXL_LOG(WARNING)
                << "StoryController::AddIntentCall::AddModCall returned "
                << "error response with message: " << result.error_message;
          }
          module_data_ = std::move(module_data);
          LaunchModuleIfStoryRunning(flow);
        });
  }

  void LaunchModuleIfStoryRunning(FlowToken flow) {
    if (story_controller_impl_->IsRunning()) {
      // TODO(thatguy): Should we be checking surface_relation also?
      if (!view_token_.value) {
        operation_queue_.Add(std::make_unique<LaunchModuleInShellCall>(
            story_controller_impl_, std::move(module_data_),
            std::move(module_controller_request_), [flow] {}));
      } else {
        operation_queue_.Add(std::make_unique<LaunchModuleCall>(
            story_controller_impl_, std::move(module_data_),
            std::move(module_controller_request_), std::move(view_token_),
            [this, flow] {
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

  // Some of the fields in add_mod_params_ are used to initializel
  // module_data_ in AddModuleFromResult().
  AddModParams add_mod_params_;
  fuchsia::ui::views::ViewToken view_token_;
  fidl::InterfaceRequest<fuchsia::modular::ModuleController>
      module_controller_request_;

  // Created by AddModuleFromResult, and ultimately written to story state.
  fuchsia::modular::ModuleData module_data_;

  fuchsia::modular::StartModuleStatus start_module_status_{
      fuchsia::modular::StartModuleStatus::NO_MODULES_FOUND};
};

class StoryControllerImpl::StartCall : public Operation<> {
 public:
  StartCall(StoryControllerImpl* const story_controller_impl,
            StoryStorage* const storage)
      : Operation("StoryControllerImpl::StartCall", [] {}),
        story_controller_impl_(story_controller_impl),
        storage_(storage) {}

 private:
  void Run() override {
    FlowToken flow{this};

    // If the story is running, we do nothing.
    if (story_controller_impl_->IsRunning()) {
      FXL_LOG(INFO)
          << "StoryControllerImpl::StartCall() while already running: ignored.";
      return;
    }

    story_controller_impl_->StartStoryShell();

    // Start all modules that were not themselves explicitly started by another
    // module.
    storage_->ReadAllModuleData()->Then(
        [this, flow](std::vector<fuchsia::modular::ModuleData> data) {
          story_controller_impl_->InitStoryEnvironment();
          for (auto& module_data : data) {
            // Don't start the module if it is embedded, or if it has been
            // marked deleted.
            if (module_data.module_deleted || module_data.is_embedded) {
              continue;
            }
            FXL_CHECK(module_data.intent);
            operation_queue_.Add(std::make_unique<LaunchModuleInShellCall>(
                story_controller_impl_, std::move(module_data),
                nullptr /* module_controller_request */, [flow] {}));
          }

          story_controller_impl_->SetRuntimeState(
              fuchsia::modular::StoryState::RUNNING);
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  StoryStorage* const storage_;                       // not owned

  OperationQueue operation_queue_;
};

class StoryControllerImpl::UpdateSnapshotCall : public Operation<> {
 public:
  UpdateSnapshotCall(StoryControllerImpl* const story_controller_impl,
                     fit::function<void()> done)
      : Operation("StoryControllerImpl::UpdateSnapshotCall", std::move(done)),
        story_controller_impl_(story_controller_impl) {}

 private:
  void Run() override {
    FlowToken flow{this};

    // If the story shell is not running, we avoid updating the snapshot.
    if (!story_controller_impl_->IsRunning()) {
      FXL_LOG(INFO) << "StoryControllerImpl::UpdateSnapshotCall() called when "
                       "story shell is not initialized.";
      return;
    }

    FlowTokenHolder branch{flow};
    // |flow| will branch into normal and timeout paths. |flow| must go out of
    // scope when either of the paths finishes. We pass a weak ptr of
    // story_controller_impl to the callback in case the operation goes out of
    // scope from timeout.
    story_controller_impl_->story_provider_impl_->TakeSnapshot(
        story_controller_impl_->story_id_,
        [weak_ptr = story_controller_impl_->weak_factory_.GetWeakPtr(),
         branch](fuchsia::mem::Buffer snapshot) {
          if (!weak_ptr) {
            return;
          }

          if (snapshot.size == 0) {
            FXL_LOG(INFO)
                << "TakeSnapshot returned an invalid snapshot for story: "
                << weak_ptr->story_id_;
            return;
          }

          // Even if the snapshot comes back after timeout, we attempt to
          // process it by loading the snapshot and saving it to storage. This
          // call assumes that the snapshot loader has already been connected.
          if (!weak_ptr->snapshot_loader_.is_bound()) {
            FXL_LOG(ERROR) << "UpdateSnapshotCall called when snapshot loader "
                              "has not been connected for story: "
                           << weak_ptr->story_id_;
          } else {
            fuchsia::mem::Buffer snapshot_copy;
            snapshot.Clone(&snapshot_copy);
            weak_ptr->snapshot_loader_->Load(std::move(snapshot_copy));
          }

          weak_ptr->session_storage_
              ->WriteSnapshot(weak_ptr->story_id_, std::move(snapshot))
              ->Then([weak_ptr, branch]() {
                auto flow = branch.Continue();
                if (!flow) {
                  FXL_LOG(INFO) << "Saved snapshot for story after timeout: "
                                << weak_ptr->story_id_;
                } else {
                  FXL_LOG(INFO)
                      << "Saved snapshot for story: " << weak_ptr->story_id_;
                }
              });
        });

    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [this, branch] {
          auto flow = branch.Continue();
          if (flow) {
            FXL_LOG(INFO) << "Timed out while updating snapshot for story: "
                          << story_controller_impl_->story_id_;
          }
        },
        kUpdateSnapshotTimeout);
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
};

class StoryControllerImpl::StartSnapshotLoaderCall : public Operation<> {
 public:
  StartSnapshotLoaderCall(StoryControllerImpl* const story_controller_impl,
                          fuchsia::ui::views::ViewToken view_token)
      : Operation("StoryControllerImpl::StartSnapshotLoaderCall", [] {}),
        story_controller_impl_(story_controller_impl),
        view_token_(std::move(view_token)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    story_controller_impl_->story_provider_impl_->StartSnapshotLoader(
        std::move(view_token_),
        story_controller_impl_->snapshot_loader_.NewRequest());

    story_controller_impl_->session_storage_
        ->ReadSnapshot(story_controller_impl_->story_id_)
        ->Then([this, flow](fuchsia::mem::BufferPtr snapshot) {
          if (!snapshot) {
            FXL_LOG(INFO)
                << "ReadSnapshot returned a null/invalid snapshot for story: "
                << story_controller_impl_->story_id_;
            return;
          }

          story_controller_impl_->snapshot_loader_->Load(
              std::move(*snapshot.get()));
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::ui::views::ViewToken view_token_;
};

StoryControllerImpl::StoryControllerImpl(
    SessionStorage* const session_storage, StoryStorage* const story_storage,
    std::unique_ptr<StoryMutator> story_mutator,
    std::unique_ptr<StoryObserver> story_observer,
    StoryVisibilitySystem* const story_visibility_system,
    StoryProviderImpl* const story_provider_impl)
    : story_id_(story_observer->model().name()),
      story_provider_impl_(story_provider_impl),
      session_storage_(session_storage),
      story_storage_(story_storage),
      story_mutator_(std::move(story_mutator)),
      story_observer_(std::move(story_observer)),
      story_visibility_system_(story_visibility_system),
      story_shell_context_impl_{story_id_, story_provider_impl, this},
      weak_factory_(this) {
  auto story_scope = fuchsia::modular::StoryScope::New();
  story_scope->story_id = story_id_;
  auto scope = fuchsia::modular::ComponentScope::New();
  scope->set_story_scope(std::move(*story_scope));
  story_provider_impl_->user_intelligence_provider()
      ->GetComponentIntelligenceServices(std::move(*scope),
                                         intelligence_services_.NewRequest());
  story_storage_->set_on_module_data_updated(
      [this](fuchsia::modular::ModuleData module_data) {
        OnModuleDataUpdated(std::move(module_data));
      });

  story_observer_->RegisterListener(
      [this](const fuchsia::modular::storymodel::StoryModel& model) {
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

fidl::VectorPtr<fuchsia::modular::OngoingActivityType>
StoryControllerImpl::GetOngoingActivities() {
  fidl::VectorPtr<fuchsia::modular::OngoingActivityType> ongoing_activities;
  ongoing_activities.resize(0);
  for (auto& entry : ongoing_activities_.bindings()) {
    ongoing_activities.push_back(entry->impl()->GetType());
  }

  return ongoing_activities;
}

void StoryControllerImpl::Sync(fit::function<void()> done) {
  operation_queue_.Add(std::make_unique<SyncCall>(std::move(done)));
}

void StoryControllerImpl::FocusModule(
    const std::vector<std::string>& module_path) {
  operation_queue_.Add(std::make_unique<FocusCall>(this, module_path));
}

void StoryControllerImpl::DefocusModule(
    const std::vector<std::string>& module_path) {
  operation_queue_.Add(std::make_unique<DefocusCall>(this, module_path));
}

void StoryControllerImpl::StopModule(
    const std::vector<std::string>& module_path, fit::function<void()> done) {
  operation_queue_.Add(std::make_unique<StopModuleCall>(
      story_storage_, module_path, std::move(done)));
}

void StoryControllerImpl::ReleaseModule(
    ModuleControllerImpl* const module_controller_impl) {
  auto fit = std::find_if(running_mod_infos_.begin(), running_mod_infos_.end(),
                          [module_controller_impl](const RunningModInfo& c) {
                            return c.module_controller_impl.get() ==
                                   module_controller_impl;
                          });
  FXL_DCHECK(fit != running_mod_infos_.end());
  fit->module_controller_impl.release();
  pending_story_shell_views_.erase(
      ModulePathToSurfaceID(fit->module_data->module_path));
  running_mod_infos_.erase(fit);
}

fidl::StringPtr StoryControllerImpl::GetStoryId() const {
  return story_observer_->model().name();
}

void StoryControllerImpl::RequestStoryFocus() {
  story_provider_impl_->RequestStoryFocus(story_id_);
}

// TODO(drees) Collapse functionality into GetLink.
void StoryControllerImpl::ConnectLinkPath(
    fuchsia::modular::LinkPathPtr link_path,
    fidl::InterfaceRequest<fuchsia::modular::Link> request) {
  // Cache a copy of the current active links, because link_impls_.AddBinding()
  // will change the set to include the newly created link connection.
  auto active_links = GetActiveLinksInternal();

  LinkPath link_path_clone;
  link_path->Clone(&link_path_clone);
  link_impls_.AddBinding(
      std::make_unique<LinkImpl>(story_storage_, std::move(link_path_clone)),
      std::move(request));
}

fuchsia::modular::LinkPathPtr StoryControllerImpl::GetLinkPathForParameterName(
    const std::vector<std::string>& module_path, std::string name) {
  auto mod_info = FindRunningModInfo(module_path);
  // NOTE: |mod_info| will only be valid if the module at |module_path| is
  // running. Strictly speaking, this is unsafe. The source of truth is the
  // Ledger, accessible through StoryStorage, but the call would be dispatcher,
  // which would change the flow of all clients of this method. For now, we
  // leave as-is.
  FXL_DCHECK(mod_info) << ModulePathToSurfaceID(module_path);

  const auto& param_map = mod_info->module_data->parameter_map;
  auto it = std::find_if(
      param_map.entries.begin(), param_map.entries.end(),
      [&name](const fuchsia::modular::ModuleParameterMapEntry& data) {
        return data.name == name;
      });

  fuchsia::modular::LinkPathPtr link_path = nullptr;
  if (it != param_map.entries.end()) {
    link_path = CloneOptional(it->link_path);
  }

  if (!link_path) {
    link_path = fuchsia::modular::LinkPath::New();
    link_path->module_path = module_path;
    link_path->link_name = name;
  }

  return link_path;
}

void StoryControllerImpl::EmbedModule(
    AddModParams add_mod_params,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController>
        module_controller_request,
    fuchsia::ui::views::ViewToken view_token,
    fit::function<void(fuchsia::modular::StartModuleStatus)> callback) {
  operation_queue_.Add(std::make_unique<AddIntentCall>(
      this, std::move(add_mod_params), std::move(module_controller_request),
      std::move(view_token), std::move(callback)));
}

void StoryControllerImpl::AddModuleToStory(
    AddModParams add_mod_params,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController>
        module_controller_request,
    fit::function<void(fuchsia::modular::StartModuleStatus)> callback) {
  operation_queue_.Add(std::make_unique<AddIntentCall>(
      this, std::move(add_mod_params), std::move(module_controller_request),
      fuchsia::ui::views::ViewToken() /* view_token */, std::move(callback)));
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

    const auto anchor_surface_id =
        ModulePathToSurfaceID(anchor->module_data->module_path);
    if (!connected_views_.count(anchor_surface_id)) {
      continue;
    }

    if (!kv.second.view_owner.is_bound()) {
      FXL_LOG(WARNING)
          << "The module view owner connection is not bound, so it "
             "can't be sent to the story shell.";
      continue;
    }

    const auto surface_id = ModulePathToSurfaceID(kv.second.module_path);
    fuchsia::modular::ViewConnection view_connection;
    view_connection.surface_id = surface_id;
    view_connection.owner = std::move(kv.second.view_owner);
    fuchsia::modular::SurfaceInfo surface_info;
    surface_info.parent_id = anchor_surface_id;
    surface_info.surface_relation = std::move(kv.second.surface_relation);
    surface_info.module_manifest = std::move(kv.second.module_manifest);
    surface_info.module_source = std::move(kv.second.module_source);
    story_shell_->AddSurface(std::move(view_connection),
                             std::move(surface_info));
    connected_views_.emplace(surface_id);

    added_keys.push_back(kv.first);
  }

  if (added_keys.size()) {
    for (auto& key : added_keys) {
      pending_story_shell_views_.erase(key);
    }
    ProcessPendingStoryShellViews();
  }
}

std::set<fuchsia::modular::LinkPath>
StoryControllerImpl::GetActiveLinksInternal() {
  std::set<fuchsia::modular::LinkPath> paths;
  for (auto& entry : link_impls_.bindings()) {
    LinkPath p;
    entry->impl()->link_path().Clone(&p);
    paths.insert(std::move(p));
  }
  return paths;
}

void StoryControllerImpl::OnModuleDataUpdated(
    fuchsia::modular::ModuleData module_data) {
  operation_queue_.Add(
      std::make_unique<OnModuleDataUpdatedCall>(this, std::move(module_data)));
}

void StoryControllerImpl::GetInfo(GetInfoCallback callback) {
  // Synced such that if GetInfo() is called after Start() or Stop(), the
  // state after the previously invoked operation is returned.
  //
  // If this call enters a race with a StoryProvider.DeleteStory() call,
  // resulting in |this| being destroyed, |callback| will be dropped.
  operation_queue_.Add(
      std::make_unique<SyncCall>([this, callback = std::move(callback)] {
        auto story_info = story_provider_impl_->GetCachedStoryInfo(story_id_);
        FXL_CHECK(story_info);
        callback(std::move(*story_info),
                 story_observer_->model().runtime_state());
      }));
}

void StoryControllerImpl::RequestStart() {
  operation_queue_.Add(std::make_unique<StartCall>(this, story_storage_));
}

void StoryControllerImpl::Stop(StopCallback done) {
  operation_queue_.Add(
      std::make_unique<StopCall>(this, false /* bulk */, std::move(done)));
}

void StoryControllerImpl::StopBulk(const bool bulk, StopCallback done) {
  operation_queue_.Add(std::make_unique<StopCall>(this, bulk, std::move(done)));
}

void StoryControllerImpl::TakeAndLoadSnapshot(
    fuchsia::ui::views::ViewToken view_token,
    TakeAndLoadSnapshotCallback done) {
  // Currently we start a new snapshot view on every TakeAndLoadSnapshot
  // invocation. We can optimize later by connecting the snapshot loader on
  // start and re-using it for the lifetime of the story.
  operation_queue_.Add(
      std::make_unique<StartSnapshotLoaderCall>(this, std::move(view_token)));
  operation_queue_.Add(
      std::make_unique<UpdateSnapshotCall>(this, std::move(done)));
}

void StoryControllerImpl::Watch(
    fidl::InterfaceHandle<fuchsia::modular::StoryWatcher> watcher) {
  auto ptr = watcher.Bind();
  NotifyOneStoryWatcher(story_observer_->model(), ptr.get());
  watchers_.AddInterfacePtr(std::move(ptr));
}

void StoryControllerImpl::GetActiveModules(GetActiveModulesCallback callback) {
  // We execute this in a SyncCall so that we are sure we don't fall in a
  // crack between a module being created and inserted in the connections
  // collection during some Operation.
  operation_queue_.Add(std::make_unique<SyncCall>(
      [this, callback = std::move(callback)]() mutable {
        std::vector<fuchsia::modular::ModuleData> result;

        result.resize(running_mod_infos_.size());
        for (size_t i = 0; i < running_mod_infos_.size(); i++) {
          running_mod_infos_[i].module_data->Clone(&result.at(i));
        }
        callback(std::move(result));
      }));
}

void StoryControllerImpl::GetModules(GetModulesCallback callback) {
  auto on_run = Future<>::Create("StoryControllerImpl.GetModules.on_run");
  auto done =
      on_run->AsyncMap([this] { return story_storage_->ReadAllModuleData(); });
  operation_queue_.Add(WrapFutureAsOperation(
      "StoryControllerImpl.GetModules.op", on_run, done, std::move(callback)));
}

void StoryControllerImpl::GetModuleController(
    std::vector<std::string> module_path,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController> request) {
  operation_queue_.Add(
      std::make_unique<SyncCall>([this, module_path = std::move(module_path),
                                  request = std::move(request)]() mutable {
        for (auto& running_mod_info : running_mod_infos_) {
          if (module_path == running_mod_info.module_data->module_path) {
            running_mod_info.module_controller_impl->Connect(
                std::move(request));
            return;
          }
        }

        // Trying to get a controller for a module that is not active just
        // drops the connection request.
      }));
}

void StoryControllerImpl::GetLink(
    fuchsia::modular::LinkPath link_path,
    fidl::InterfaceRequest<fuchsia::modular::Link> request) {
  ConnectLinkPath(fidl::MakeOptional(std::move(link_path)), std::move(request));
}

void StoryControllerImpl::StartStoryShell() {
  auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

  story_shell_holder_ = story_provider_impl_->StartStoryShell(
      story_id_, std::move(view_token), story_shell_.NewRequest());

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

void StoryControllerImpl::SetRuntimeState(
    const fuchsia::modular::StoryState new_state) {
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

bool StoryControllerImpl::IsExternalModule(
    const std::vector<std::string>& module_path) {
  auto* const i = FindRunningModInfo(module_path);
  if (!i) {
    return false;
  }

  return i->module_data->module_source ==
         fuchsia::modular::ModuleSource::EXTERNAL;
}

StoryControllerImpl::RunningModInfo* StoryControllerImpl::FindRunningModInfo(
    const std::vector<std::string>& module_path) {
  for (auto& c : running_mod_infos_) {
    if (c.module_data->module_path == module_path) {
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

  auto* anchor = FindRunningModInfo(
      ParentModulePath(running_mod_info->module_data->module_path));

  // Traverse up until there is a non-embedded module.
  while (anchor && anchor->module_data->is_embedded) {
    anchor =
        FindRunningModInfo(ParentModulePath(anchor->module_data->module_path));
  }

  return anchor;
}

void StoryControllerImpl::RemoveModuleFromStory(
    const std::vector<std::string>& module_path) {
  operation_queue_.Add(std::make_unique<StopModuleAndStoryIfEmptyCall>(
      this, module_path, [] {}));
}

void StoryControllerImpl::InitStoryEnvironment() {
  FXL_DCHECK(!story_environment_)
      << "Story scope already running for story_id = " << story_id_;

  static const auto* const kEnvServices =
      new std::vector<std::string>{fuchsia::modular::ContextWriter::Name_};
  story_environment_ = std::make_unique<Environment>(
      story_provider_impl_->user_environment(),
      kStoryEnvironmentLabelPrefix + story_id_.get(), *kEnvServices,
      /* kill_on_oom = */ false);
  story_environment_->AddService<fuchsia::modular::ContextWriter>(
      [this](fidl::InterfaceRequest<fuchsia::modular::ContextWriter> request) {
        intelligence_services_->GetContextWriter(std::move(request));
      });
}

void StoryControllerImpl::DestroyStoryEnvironment() {
  story_environment_.reset();
}

void StoryControllerImpl::StartOngoingActivity(
    const fuchsia::modular::OngoingActivityType ongoing_activity_type,
    fidl::InterfaceRequest<fuchsia::modular::OngoingActivity> request) {
  // Newly created/destroyed ongoing activities should be dispatched to the
  // story provider.
  auto dispatch_to_story_provider = [this] {
    story_provider_impl_->NotifyStoryActivityChange(story_id_,
                                                    GetOngoingActivities());
  };

  // When a connection is closed on the client-side, the OngoingActivityImpl is
  // destroyed after it is removed from the binding set, so we dispatch to the
  // story provider in the destructor of OngoingActivityImpl.
  ongoing_activities_.AddBinding(
      std::make_unique<OngoingActivityImpl>(
          ongoing_activity_type,
          /* on_destroy= */ dispatch_to_story_provider),
      std::move(request));

  // Conversely, when a connection is created, the OngoingActivityImpl is
  // initialized before added to the binding set, so we need to dispatch after
  // bind.
  dispatch_to_story_provider();
}

void StoryControllerImpl::CreateEntity(
    std::string type, fuchsia::mem::Buffer data,
    fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request,
    fit::function<void(std::string /* entity_reference */)> callback) {
  story_provider_impl_->CreateEntity(story_id_, type, std::move(data),
                                     std::move(entity_request),
                                     std::move(callback));
}

void StoryControllerImpl::OnSurfaceFocused(fidl::StringPtr surface_id) {
  auto module_path = ModulePathFromSurfaceID(surface_id);

  for (auto& watcher : watchers_.ptrs()) {
    (*watcher)->OnModuleFocused(std::move(module_path));
  }
}

}  // namespace modular
