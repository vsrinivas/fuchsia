// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/story_runner/story_controller_impl.h"

#include <memory>
#include <string>
#include <vector>

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/async/cpp/future.h>
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
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/join_strings.h>
#include <lib/fxl/type_converter.h>

#include "peridot/bin/device_runner/cobalt/cobalt.h"
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/find_modules_call.h"
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/get_types_from_entity_call.h"
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/initialize_chain_call.h"
#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/bin/user_runner/storage/story_storage.h"
#include "peridot/bin/user_runner/story_runner/link_impl.h"
#include "peridot/bin/user_runner/story_runner/module_context_impl.h"
#include "peridot/bin/user_runner/story_runner/module_controller_impl.h"
#include "peridot/bin/user_runner/story_runner/story_provider_impl.h"
#include "peridot/lib/common/names.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/ledger_client/operations.h"

// Used to create std::set<LinkPath>.
namespace std {
template <>
struct less<fuchsia::modular::LinkPath> {
  bool operator()(const fuchsia::modular::LinkPath& left,
                  const fuchsia::modular::LinkPath& right) const {
    if (left.module_path == right.module_path) {
      return left.link_name < right.link_name;
    }
    return *left.module_path < *right.module_path;
  }
};
}  // namespace std

namespace modular {

constexpr char kStoryScopeLabelPrefix[] = "story-";

namespace {

fidl::StringPtr PathString(
    const fidl::VectorPtr<fidl::StringPtr>& module_path) {
  auto path = fxl::To<std::vector<std::string>>(module_path);
  return fxl::JoinStrings(path, ":");
}

fidl::VectorPtr<fidl::StringPtr> ParentModulePath(
    const fidl::VectorPtr<fidl::StringPtr>& module_path) {
  fidl::VectorPtr<fidl::StringPtr> ret =
      fidl::VectorPtr<fidl::StringPtr>::New(0);

  if (module_path->size() > 0) {
    for (size_t i = 0; i < module_path->size() - 1; i++) {
      ret.push_back(module_path->at(i));
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

  if (old_intent.action != new_intent.action) {
    return true;
  }

  std::map<fidl::StringPtr, const fuchsia::modular::IntentParameterData*>
      old_params;
  if (old_intent.parameters) {
    for (const auto& entry : *old_intent.parameters) {
      old_params[entry.name] = &entry.data;
    }
  }

  std::map<fidl::StringPtr, const fuchsia::modular::IntentParameterData*>
      new_params;
  if (new_intent.parameters) {
    for (const auto& entry : *new_intent.parameters) {
      new_params[entry.name] = &entry.data;
    }
  }

  if (new_params.size() != old_params.size()) {
    return true;
  }

  for (const auto& entry : new_params) {
    const auto& name = entry.first;
    if (old_params.count(name) == 0) {
      return true;
    }

    const auto& new_param = *entry.second;
    const auto& old_param = *old_params[name];

    // If a parameter type changed, or a link mapping changed, we
    // need to relaunch.
    if (old_param.Which() != new_param.Which()) {
      return true;
    }

    if (old_param.is_link_name() &&
        old_param.link_name() != new_param.link_name()) {
      return true;
    }
    if (old_param.is_link_path() &&
        old_param.link_path() != new_param.link_path()) {
      return true;
    }

    // For now, if the param is static data (ie, json or entity_reference), we
    // do NOT want to force restart, even if the data is different.
  }

  return false;
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
                   fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
                       view_owner_request,
                   ResultCall result_call)
      : Operation("StoryControllerImpl::LaunchModuleCall",
                  std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)),
        module_controller_request_(std::move(module_controller_request)),
        view_owner_request_(std::move(view_owner_request)),
        start_time_(zx_clock_get(ZX_CLOCK_UTC)) {
    FXL_DCHECK(!module_data_.module_path.is_null());
  }

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
  }

  void Launch(FlowToken /*flow*/) {
    FXL_LOG(INFO) << "StoryControllerImpl::LaunchModule() "
                  << module_data_.module_url << " "
                  << PathString(module_data_.module_path);
    fuchsia::modular::AppConfig module_config;
    module_config.url = module_data_.module_url;

    fuchsia::ui::viewsv1::ViewProviderPtr view_provider;
    fidl::InterfaceRequest<fuchsia::ui::viewsv1::ViewProvider>
        view_provider_request = view_provider.NewRequest();
    view_provider->CreateView(std::move(view_owner_request_), nullptr);

    fuchsia::sys::ServiceProviderPtr module_context_provider;
    auto module_context_provider_request = module_context_provider.NewRequest();
    auto service_list = fuchsia::sys::ServiceList::New();
    service_list->names.push_back(fuchsia::modular::ModuleContext::Name_);
    service_list->provider = std::move(module_context_provider);

    RunningModInfo running_mod_info;
    running_mod_info.module_data = CloneOptional(module_data_);

    // ModuleControllerImpl's constructor launches the child application.
    running_mod_info.module_controller_impl =
        std::make_unique<ModuleControllerImpl>(
            story_controller_impl_,
            story_controller_impl_->story_scope_.GetLauncher(),
            std::move(module_config), running_mod_info.module_data.get(),
            std::move(service_list), std::move(view_provider_request));

    // Modules started with
    // fuchsia::modular::StoryController.fuchsia::modular::AddModule() don't
    // have a module controller request.
    if (module_controller_request_.is_valid()) {
      running_mod_info.module_controller_impl->Connect(
          std::move(module_controller_request_));
    }

    ModuleContextInfo module_context_info = {
        story_controller_impl_->story_provider_impl_->component_context_info(),
        story_controller_impl_,
        story_controller_impl_->story_provider_impl_
            ->user_intelligence_provider()};

    running_mod_info.module_context_impl = std::make_unique<ModuleContextImpl>(
        module_context_info, running_mod_info.module_data.get(),
        std::move(module_context_provider_request));

    story_controller_impl_->running_mod_infos_.emplace_back(
        std::move(running_mod_info));

    for (auto& i : story_controller_impl_->watchers_.ptrs()) {
      fuchsia::modular::ModuleData module_data;
      module_data_.Clone(&module_data);
      (*i)->OnModuleAdded(std::move(module_data));
    }

    for (auto& i : story_controller_impl_->modules_watchers_.ptrs()) {
      fuchsia::modular::ModuleData module_data;
      module_data_.Clone(&module_data);
      (*i)->OnNewModule(std::move(module_data));
    }

    ReportModuleLaunchTime(module_data_.module_url,
                           zx_clock_get(ZX_CLOCK_UTC) - start_time_);
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::modular::ModuleData module_data_;
  fidl::InterfaceRequest<fuchsia::modular::ModuleController>
      module_controller_request_;
  fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
      view_owner_request_;
  const zx_time_t start_time_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LaunchModuleCall);
};

class StoryControllerImpl::KillModuleCall : public Operation<> {
 public:
  KillModuleCall(StoryControllerImpl* const story_controller_impl,
                 fuchsia::modular::ModuleData module_data,
                 const std::function<void()>& done)
      : Operation("StoryControllerImpl::KillModuleCall", [] {}),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)),
        done_(done) {}

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
      story_controller_impl_->story_shell_->DefocusView(
          PathString(module_data_.module_path), future->Completer());
    } else {
      future->Complete();
    }

    future->Then([this, flow] {
      // Teardown the module, which discards the module controller. A parent
      // module can call fuchsia::modular::ModuleController.Stop() multiple
      // times before the fuchsia::modular::ModuleController connection gets
      // disconnected by Teardown(). Therefore, this StopModuleCall Operation
      // will cause the calls to be queued. The first Stop() will cause the
      // fuchsia::modular::ModuleController to be closed, and so subsequent
      // Stop() attempts will not find a controller and will return.
      auto* const running_mod_info =
          story_controller_impl_->FindRunningModInfo(module_data_.module_path);

      if (!running_mod_info) {
        FXL_LOG(INFO) << "No ModuleController for Module"
                      << " " << PathString(module_data_.module_path) << ". "
                      << "Was ModuleController.Stop() called twice?";
        done_();
        return;
      }

      // done_() must be called BEFORE the Teardown() done callback returns. See
      // comment in StopModuleCall::Kill() before making changes here. Be aware
      // that done_ is NOT the Done() callback of the Operation.
      running_mod_info->module_controller_impl->Teardown([this, flow] {
        for (auto& i : story_controller_impl_->modules_watchers_.ptrs()) {
          fuchsia::modular::ModuleData module_data;
          module_data_.Clone(&module_data);
          (*i)->OnStopModule(std::move(module_data));
        }
        done_();
      });
    });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::modular::ModuleData module_data_;
  std::function<void()> done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(KillModuleCall);
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
    operation_queue_.Add(new LaunchModuleCall(
        story_controller_impl_, fidl::Clone(module_data_),
        std::move(module_controller_request_), view_owner_.NewRequest(),
        [this, flow] { Cont(flow); }));
  }

  void Cont(FlowToken flow) {
    // If this is called during Stop(), story_shell_ might already have been
    // reset. TODO(mesch): Then the whole operation should fail.
    if (!story_controller_impl_->story_shell_) {
      return;
    }

    // We only add a module to story shell if its either a root module or its
    // anchor is already known to story shell.

    if (module_data_.module_path->size() == 1) {
      ConnectView(flow, "");
      return;
    }

    auto* const running_mod_info =
        story_controller_impl_->FindRunningModInfo(module_data_.module_path);
    FXL_CHECK(running_mod_info);  // Was just created.

    auto* const anchor = story_controller_impl_->FindAnchor(running_mod_info);
    if (anchor) {
      const auto anchor_view_id = PathString(anchor->module_data->module_path);
      if (story_controller_impl_->connected_views_.count(anchor_view_id)) {
        ConnectView(flow, anchor_view_id);
        return;
      }
    }

    auto manifest_clone = fuchsia::modular::ModuleManifest::New();
    fidl::Clone(module_data_.module_manifest, &manifest_clone);
    auto surface_relation_clone = fuchsia::modular::SurfaceRelation::New();
    module_data_.surface_relation->Clone(surface_relation_clone.get());
    story_controller_impl_->pending_views_.emplace(
        PathString(module_data_.module_path),
        PendingView{module_data_.module_path.Clone(), std::move(manifest_clone),
                    std::move(surface_relation_clone), std::move(view_owner_)});
  }

  void ConnectView(FlowToken flow, fidl::StringPtr anchor_view_id) {
    const auto view_id = PathString(module_data_.module_path);

    story_controller_impl_->story_shell_->AddView(
        std::move(view_owner_), view_id, anchor_view_id,
        std::move(module_data_.surface_relation),
        std::move(module_data_.module_manifest));

    story_controller_impl_->connected_views_.emplace(view_id);
    story_controller_impl_->ProcessPendingViews();
    story_controller_impl_->story_shell_->FocusView(view_id, anchor_view_id);
  }

  StoryControllerImpl* const story_controller_impl_;
  fuchsia::modular::ModuleData module_data_;
  fidl::InterfaceRequest<fuchsia::modular::ModuleController>
      module_controller_request_;

  fuchsia::modular::ModuleControllerPtr module_controller_;
  fuchsia::ui::viewsv1token::ViewOwnerPtr view_owner_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LaunchModuleInShellCall);
};

class StoryControllerImpl::StopCall : public Operation<> {
 public:
  StopCall(StoryControllerImpl* const story_controller_impl, const bool notify,
           std::function<void()> done)
      : Operation("StoryControllerImpl::StopCall", done),
        story_controller_impl_(story_controller_impl),
        notify_(notify) {}

 private:
  // StopCall may be run even on a story impl that is not running.
  void Run() override {
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
          if (story_controller_impl_->story_shell_) {
            story_controller_impl_->story_shell_app_->Teardown(
                kBasicTimeout, did_teardown->Completer());
          } else {
            did_teardown->Complete();
          }

          return did_teardown;
        })
        ->AsyncMap([this] {
          story_controller_impl_->story_shell_app_.reset();
          story_controller_impl_->story_shell_.Unbind();
          if (story_controller_impl_->story_context_binding_.is_bound()) {
            // Close() checks if called while not bound.
            story_controller_impl_->story_context_binding_.Unbind();
          }

          // Ensure every story storage operation has completed.
          return story_controller_impl_->story_storage_->Sync();
        })
        ->Then([this] {
          // Clear the remaining links and connections in case there are some
          // left. At this point, no DisposeLink() calls can arrive anymore.
          story_controller_impl_->link_impls_.CloseAll();

          // If this StopCall is part of a DeleteCall, then we don't notify
          // story state changes; the pertinent state change will be the delete
          // notification instead.
          if (notify_) {
            story_controller_impl_->SetState(
                fuchsia::modular::StoryState::STOPPED);
          } else {
            story_controller_impl_->state_ =
                fuchsia::modular::StoryState::STOPPED;
          }

          Done();
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const bool notify_;  // Whether to notify state change; false in DeleteCall.

  FXL_DISALLOW_COPY_AND_ASSIGN(StopCall);
};

class StoryControllerImpl::StopModuleCall : public Operation<> {
 public:
  StopModuleCall(StoryControllerImpl* const story_controller_impl,
                 StoryStorage* const storage,
                 const fidl::VectorPtr<fidl::StringPtr>& module_path,
                 const std::function<void()>& done)
      : Operation("StoryControllerImpl::StopModuleCall", done),
        story_controller_impl_(story_controller_impl),
        storage_(storage),
        module_path_(module_path.Clone()) {}

 private:
  void Run() override {
    // NOTE(alhaad): We don't use flow tokens here. See NOTE below to know
    // why.

    // Mark this module as stopped, which is a global state shared between
    // machines to track when the module is explicitly stopped. Then, run
    // KillModuleCall, which will tear down the running instance.
    storage_
        ->UpdateModuleData(
            module_path_,
            [this](fuchsia::modular::ModuleDataPtr* module_data_ptr) {
              FXL_DCHECK(*module_data_ptr);
              (*module_data_ptr)->module_stopped = true;
              (*module_data_ptr)->Clone(&cached_module_data_);
            })
        ->WeakAsyncMap(
            GetWeakPtr(),
            [this] {
              auto did_kill_module = Future<>::Create(
                  "StoryControllerImpl.StopModuleCall.Run.did_kill_module");
              operation_queue_.Add(new KillModuleCall(
                  story_controller_impl_, std::move(cached_module_data_),
                  did_kill_module->Completer()));
              return did_kill_module;
            })
        ->Then([this] {
          // NOTE(alhaad): An interesting flow of control to keep in mind:
          //
          // 1. From fuchsia::modular::ModuleController.Stop() which can only be
          // called from FIDL, we call StoryControllerImpl.StopModule().
          //
          // 2.  StoryControllerImpl.StopModule() pushes StopModuleCall onto the
          // operation queue.
          //
          // 3. When operation becomes current, we write to ledger, block and
          // continue on receiving OnPageChange from ledger.
          //
          // 4. We then call KillModuleCall on a sub operation queue.
          //
          // 5. KillModuleCall will call Teardown() on the same
          // ModuleControllerImpl that had started
          // fuchsia::modular::ModuleController.Stop(). In the callback from
          // Teardown(), it calls done_() (and NOT Done()).
          //
          // 6. done_() in KillModuleCall leads to the next line here, which
          // calls Done() which would call the FIDL callback from
          // fuchsia::modular::ModuleController.Stop().
          //
          // 7. Done() on the next line also deletes this which deletes the
          // still running KillModuleCall, but this is okay because the only
          // thing that was left to do in KillModuleCall was FlowToken going out
          // of scope.
          Done();
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  StoryStorage* const storage_;                       // not owned
  const fidl::VectorPtr<fidl::StringPtr> module_path_;
  fuchsia::modular::ModuleData cached_module_data_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StopModuleCall);
};

class StoryControllerImpl::StopModuleAndStoryIfEmptyCall : public Operation<> {
 public:
  StopModuleAndStoryIfEmptyCall(
      StoryControllerImpl* const story_controller_impl,
      const fidl::VectorPtr<fidl::StringPtr>& module_path,
      const std::function<void()>& done)
      : Operation("StoryControllerImpl::StopModuleAndStoryIfEmptyCall", done),
        story_controller_impl_(story_controller_impl),
        module_path_(module_path.Clone()) {}

 private:
  void Run() override {
    FlowToken flow{this};
    operation_queue_.Add(new StopModuleCall(
        story_controller_impl_, story_controller_impl_->story_storage_,
        module_path_, [this, flow] {
          if (story_controller_impl_->running_mod_infos_.empty()) {
            operation_queue_.Add(new StopCall(story_controller_impl_,
                                              true /* notify */, [flow] {}));
          }
        }));
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const fidl::VectorPtr<fidl::StringPtr> module_path_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StopModuleAndStoryIfEmptyCall);
};

class StoryControllerImpl::DeleteCall : public Operation<> {
 public:
  DeleteCall(StoryControllerImpl* const story_controller_impl,
             std::function<void()> done)
      : Operation("StoryControllerImpl::DeleteCall", [] {}),
        story_controller_impl_(story_controller_impl),
        done_(std::move(done)) {}

 private:
  void Run() override {
    // No call to Done(), in order to block all further operations on the queue
    // until the instance is deleted.
    operation_queue_.Add(
        new StopCall(story_controller_impl_, false /* notify */, done_));
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned

  // Not the result call of the Operation, because it's invoked without
  // unblocking the operation queue, to prevent subsequent operations from
  // executing until the instance is deleted, which cancels those operations.
  std::function<void()> done_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteCall);
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
    if (!story_controller_impl_->IsRunning() ||
        module_data_.module_source !=
            fuchsia::modular::ModuleSource::EXTERNAL) {
      return;
    }

    // Check for existing module at the given path.
    auto* const running_mod_info =
        story_controller_impl_->FindRunningModInfo(module_data_.module_path);
    if (module_data_.module_stopped) {
      // If the module is running, kill it.
      if (running_mod_info) {
        operation_queue_.Add(new KillModuleCall(
            story_controller_impl_, std::move(module_data_), [flow] {}));
      }
      return;
    }

    // We reach this point only if we want to start or update an existing
    // external module.
    operation_queue_.Add(new LaunchModuleInShellCall(
        story_controller_impl_, std::move(module_data_),
        nullptr /* module_controller_request */, [flow] {}));
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  fuchsia::modular::ModuleData module_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(OnModuleDataUpdatedCall);
};

class StoryControllerImpl::FocusCall : public Operation<> {
 public:
  FocusCall(StoryControllerImpl* const story_controller_impl,
            fidl::VectorPtr<fidl::StringPtr> module_path)
      : Operation("StoryControllerImpl::FocusCall", [] {}),
        story_controller_impl_(story_controller_impl),
        module_path_(std::move(module_path)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    if (!story_controller_impl_->story_shell_) {
      return;
    }

    RunningModInfo* const anchor = story_controller_impl_->FindAnchor(
        story_controller_impl_->FindRunningModInfo(module_path_));
    if (anchor) {
      // Focus modules relative to their anchor module.
      story_controller_impl_->story_shell_->FocusView(
          PathString(module_path_),
          PathString(anchor->module_data->module_path));
    } else {
      // Focus root modules absolutely.
      story_controller_impl_->story_shell_->FocusView(PathString(module_path_),
                                                      nullptr);
    }
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  const fidl::VectorPtr<fidl::StringPtr> module_path_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FocusCall);
};

class StoryControllerImpl::DefocusCall : public Operation<> {
 public:
  DefocusCall(StoryControllerImpl* const story_controller_impl,
              fidl::VectorPtr<fidl::StringPtr> module_path)
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
    story_controller_impl_->story_shell_->DefocusView(PathString(module_path_),
                                                      [] {});
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  const fidl::VectorPtr<fidl::StringPtr> module_path_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DefocusCall);
};

// An operation that first performs module resolution with the provided
// fuchsia::modular::Intent and subsequently starts the most appropriate
// resolved module in the story shell.
class StoryControllerImpl::AddIntentCall
    : public Operation<fuchsia::modular::StartModuleStatus> {
 public:
  AddIntentCall(StoryControllerImpl* const story_controller_impl,
                fidl::VectorPtr<fidl::StringPtr> requesting_module_path,
                const std::string& module_name,
                fuchsia::modular::IntentPtr intent,
                fidl::InterfaceRequest<fuchsia::modular::ModuleController>
                    module_controller_request,
                fuchsia::modular::SurfaceRelationPtr surface_relation,
                fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
                    view_owner_request,
                const fuchsia::modular::ModuleSource module_source,
                ResultCall result_call)
      : Operation("StoryControllerImpl::AddIntentCall", std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        requesting_module_path_(std::move(requesting_module_path)),
        module_name_(module_name),
        intent_(std::move(intent)),
        module_controller_request_(std::move(module_controller_request)),
        surface_relation_(std::move(surface_relation)),
        view_owner_request_(std::move(view_owner_request)),
        module_source_(module_source) {}

 private:
  void Run() {
    FlowToken flow{this, &start_module_status_};
    operation_queue_.Add(new FindModulesCall(
        story_controller_impl_->story_storage_,
        story_controller_impl_->story_provider_impl_->module_resolver(),
        story_controller_impl_->story_provider_impl_->entity_resolver(),
        CloneOptional(intent_), requesting_module_path_.Clone(),
        [this, flow](fuchsia::modular::ExecuteResult result,
                     fuchsia::modular::FindModulesResponse response) {
          if (result.status != fuchsia::modular::ExecuteStatus::OK) {
            FXL_LOG(WARNING)
                << "StoryController::FindModulesCall returned "
                << "error response with message: " << result.error_message;
          }
          AddModuleFromResult(flow, std::move(response));
        }));
  }

  void AddModuleFromResult(FlowToken flow,
                           fuchsia::modular::FindModulesResponse response) {
    if (response.results->empty()) {
      start_module_status_ =
          fuchsia::modular::StartModuleStatus::NO_MODULES_FOUND;
      return;
    }

    // Add the resulting module to story state.
    const auto& module_result = response.results->at(0);
    auto create_parameter_map_info =
        PopulateCreateParameterMapInfo(requesting_module_path_, intent_);

    module_data_.module_url = module_result.module_id;
    module_data_.module_path = requesting_module_path_.Clone();
    module_data_.module_path.push_back(module_name_);
    module_data_.module_source = module_source_;
    fidl::Clone(surface_relation_, &module_data_.surface_relation);
    module_data_.module_stopped = false;
    module_data_.intent = std::move(intent_);
    fidl::Clone(module_result.manifest, &module_data_.module_manifest);

    // Initialize the chain, which we need to do to get
    // fuchsia::modular::ModuleParameterMap, which belongs in |module_data_|.
    operation_queue_.Add(new InitializeChainCall(
        story_controller_impl_->story_storage_,
        fidl::Clone(module_data_.module_path),
        std::move(create_parameter_map_info),
        [this, flow](fuchsia::modular::ModuleParameterMapPtr parameter_map) {
          WriteModuleData(flow, std::move(parameter_map));
        }));
  }

  fuchsia::modular::CreateModuleParameterMapInfoPtr
  PopulateCreateParameterMapInfo(
      const fidl::VectorPtr<fidl::StringPtr>& requesting_module_path,
      const fuchsia::modular::IntentPtr& intent) {
    auto param_map = fuchsia::modular::CreateModuleParameterMapInfo::New();
    for (auto& param : *intent->parameters) {
      if (param.data.is_entity_reference()) {
        fuchsia::modular::CreateLinkInfo create_link;
        fsl::SizedVmo vmo;
        FXL_CHECK(fsl::VmoFromString(
            EntityReferenceToJson(param.data.entity_reference()), &vmo));
        create_link.initial_data = std::move(vmo).ToTransport();
        fuchsia::modular::CreateModuleParameterMapEntry entry;
        entry.key = param.name;
        entry.value.set_create_link(std::move(create_link));
        param_map->property_info.push_back(std::move(entry));
      } else if (param.data.is_json()) {
        fuchsia::modular::CreateLinkInfo create_link;
        param.data.json().Clone(&create_link.initial_data);
        fuchsia::modular::CreateModuleParameterMapEntry entry;
        entry.key = param.name;
        entry.value.set_create_link(std::move(create_link));
        param_map->property_info.push_back(std::move(entry));
      } else if (param.data.is_link_name() || param.data.is_link_path()) {
        LinkPath lp;
        if (param.data.is_link_name()) {
          lp = std::move(*story_controller_impl_->GetLinkPathForParameterName(
              requesting_module_path, param.data.link_name()));
        } else {
          param.data.link_path().Clone(&lp);
        }

        fuchsia::modular::CreateModuleParameterMapEntry entry;
        entry.key = param.name;
        entry.value.set_link_path(std::move(lp));
        param_map->property_info.push_back(std::move(entry));
      } else if (param.data.is_entity_type()) {
        // Create a link, but don't populate it. This is useful in the event
        // that the link is used as an 'output' link.
        fuchsia::modular::CreateModuleParameterMapEntry entry;
        entry.key = param.name;
        entry.value.set_create_link(fuchsia::modular::CreateLinkInfo{});
        param_map->property_info.push_back(std::move(entry));
      } else {
        FXL_DCHECK(false) << "Unhandled intent parameter type";
      }
    }
    return param_map;
  }

  void WriteModuleData(FlowToken flow,
                       fuchsia::modular::ModuleParameterMapPtr parameter_map) {
    fidl::Clone(*parameter_map, &module_data_.parameter_map);
    // Write the module's data.
    fuchsia::modular::ModuleData module_data_copy;
    module_data_.Clone(&module_data_copy);
    story_controller_impl_->story_storage_
        ->WriteModuleData(std::move(module_data_copy))
        ->WeakThen(GetWeakPtr(), [this, flow] { MaybeLaunchModule(flow); });
  }

  void MaybeLaunchModule(FlowToken flow) {
    if (story_controller_impl_->IsRunning()) {
      // TODO(thatguy): Should we be checking surface_relation also?
      if (!view_owner_request_) {
        operation_queue_.Add(new LaunchModuleInShellCall(
            story_controller_impl_, std::move(module_data_),
            std::move(module_controller_request_), [flow] {}));
      } else {
        operation_queue_.Add(new LaunchModuleCall(
            story_controller_impl_, std::move(module_data_),
            std::move(module_controller_request_),
            std::move(view_owner_request_), [this, flow] {
              // LaunchModuleInShellCall above already calls
              // ProcessPendingViews(). NOTE(thatguy): This
              // cannot be moved into LaunchModuleCall, because
              // LaunchModuleInShellCall uses LaunchModuleCall
              // as the very first step of its operation. This
              // would inform the story shell of a new module
              // before we had told it about its
              // surface-relation parent (which we do as the
              // second part of LaunchModuleInShellCall).  So
              // we must defer to here.
              story_controller_impl_->ProcessPendingViews();
            }));
      }
    }

    start_module_status_ = fuchsia::modular::StartModuleStatus::SUCCESS;
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;

  // Arguments passed in from the constructor. Some are used to initialize
  // module_data_ in AddModuleFromResult().
  fidl::VectorPtr<fidl::StringPtr> requesting_module_path_;
  const std::string module_name_;
  fuchsia::modular::IntentPtr intent_;
  fidl::InterfaceRequest<fuchsia::modular::ModuleController>
      module_controller_request_;
  fuchsia::modular::SurfaceRelationPtr surface_relation_;
  fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
      view_owner_request_;
  const fuchsia::modular::ModuleSource module_source_;

  // Returned to us from the resolver, and cached here so that InitializeChain()
  // has access to it.
  fuchsia::modular::CreateModuleParameterMapInfoPtr create_parameter_map_info_;

  // Created by AddModuleFromResult, and ultimately written to story state.
  fuchsia::modular::ModuleData module_data_;

  fuchsia::modular::StartModuleStatus start_module_status_{
      fuchsia::modular::StartModuleStatus::NO_MODULES_FOUND};

  FXL_DISALLOW_COPY_AND_ASSIGN(AddIntentCall);
};

class StoryControllerImpl::StartContainerInShellCall : public Operation<> {
 public:
  StartContainerInShellCall(
      StoryControllerImpl* const story_controller_impl,
      fidl::VectorPtr<fidl::StringPtr> parent_module_path,
      fidl::StringPtr container_name,
      fuchsia::modular::SurfaceRelationPtr parent_relation,
      fidl::VectorPtr<fuchsia::modular::ContainerLayout> layout,
      fidl::VectorPtr<fuchsia::modular::ContainerRelationEntry> relationships,
      fidl::VectorPtr<fuchsia::modular::ContainerNodePtr> nodes)
      : Operation("StoryControllerImpl::StartContainerInShellCall", [] {}),
        story_controller_impl_(story_controller_impl),
        parent_module_path_(std::move(parent_module_path)),
        container_name_(container_name),
        parent_relation_(std::move(parent_relation)),
        layout_(std::move(layout)),
        relationships_(std::move(relationships)),
        nodes_(std::move(nodes)) {
    for (auto& relationship : *relationships_) {
      relation_map_[relationship.node_name] = CloneOptional(relationship);
    }
  }

 private:
  void Run() override {
    FlowToken flow{this};
    // parent + container used as module path of requesting module for
    // containers
    fidl::VectorPtr<fidl::StringPtr> module_path = parent_module_path_.Clone();
    // module_path.push_back(container_name_);
    // Adding non-module 'container_name_' to the module path results in
    // Ledger Client issuing a ReadData() call and failing with a fatal error
    // when module_data cannot be found
    // TODO(djmurphy): follow up, probably make containers modules
    std::vector<FuturePtr<fuchsia::modular::StartModuleStatus>> did_add_intents;
    did_add_intents.reserve(nodes_->size());

    for (size_t i = 0; i < nodes_->size(); ++i) {
      auto did_add_intent = Future<fuchsia::modular::StartModuleStatus>::Create(
          "StoryControllerImpl.StartContainerInShellCall.Run.did_add_intent");
      auto intent = fuchsia::modular::Intent::New();
      nodes_->at(i)->intent.Clone(intent.get());
      operation_queue_.Add(new AddIntentCall(
          story_controller_impl_, parent_module_path_.Clone(),
          nodes_->at(i)->node_name, std::move(intent),
          nullptr /* module_controller_request */,
          fidl::MakeOptional(
              relation_map_[nodes_->at(i)->node_name]->relationship),
          nullptr /* view_owner_request */,
          fuchsia::modular::ModuleSource::INTERNAL,
          did_add_intent->Completer()));

      did_add_intents.emplace_back(did_add_intent);
    }

    Wait<Future<>>("StoryControllerImpl.StartContainerInShellCall.Run.Wait",
                   did_add_intents)
        ->Then([this, flow] {
          if (!story_controller_impl_->story_shell_) {
            return;
          }
          auto views = fidl::VectorPtr<fuchsia::modular::ContainerView>::New(
              nodes_->size());
          for (size_t i = 0; i < nodes_->size(); i++) {
            fuchsia::modular::ContainerView view;
            view.node_name = nodes_->at(i)->node_name;
            view.owner = std::move(node_views_[nodes_->at(i)->node_name]);
            views->at(i) = std::move(view);
          }
          story_controller_impl_->story_shell_->AddContainer(
              container_name_, PathString(parent_module_path_),
              std::move(*parent_relation_), std::move(layout_),
              std::move(relationships_), std::move(views));
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  OperationQueue operation_queue_;
  const fidl::VectorPtr<fidl::StringPtr> parent_module_path_;
  const fidl::StringPtr container_name_;

  fuchsia::modular::SurfaceRelationPtr parent_relation_;
  fidl::VectorPtr<fuchsia::modular::ContainerLayout> layout_;
  fidl::VectorPtr<fuchsia::modular::ContainerRelationEntry> relationships_;
  const fidl::VectorPtr<fuchsia::modular::ContainerNodePtr> nodes_;
  std::map<std::string, fuchsia::modular::ContainerRelationEntryPtr>
      relation_map_;

  // map of node_name to view_owners
  std::map<fidl::StringPtr, fuchsia::ui::viewsv1token::ViewOwnerPtr>
      node_views_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StartContainerInShellCall);
};

class StoryControllerImpl::StartCall : public Operation<> {
 public:
  StartCall(
      StoryControllerImpl* const story_controller_impl,
      StoryStorage* const storage,
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> request)
      : Operation("StoryControllerImpl::StartCall", [] {}),
        story_controller_impl_(story_controller_impl),
        storage_(storage),
        request_(std::move(request)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    // If the story is running, we do nothing and close the view owner request.
    if (story_controller_impl_->IsRunning()) {
      FXL_LOG(INFO)
          << "StoryControllerImpl::StartCall() while already running: ignored.";
      return;
    }

    story_controller_impl_->StartStoryShell(std::move(request_));

    // Start all modules that were not themselves explicitly started by another
    // module.
    storage_->ReadAllModuleData()->Then(
        [this, flow](fidl::VectorPtr<fuchsia::modular::ModuleData> data) {
          for (auto& module_data : *data) {
            if (module_data.module_source !=
                    fuchsia::modular::ModuleSource::EXTERNAL ||
                module_data.module_stopped) {
              continue;
            }
            FXL_CHECK(module_data.intent);
            operation_queue_.Add(new LaunchModuleInShellCall(
                story_controller_impl_, std::move(module_data),
                nullptr /* module_controller_request */, [flow] {}));
          }

          story_controller_impl_->SetState(
              fuchsia::modular::StoryState::RUNNING);
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  StoryStorage* const storage_;                       // not owned
  fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> request_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StartCall);
};

StoryControllerImpl::StoryControllerImpl(
    fidl::StringPtr story_id, StoryStorage* const story_storage,
    StoryProviderImpl* const story_provider_impl)
    : story_id_(story_id),
      story_provider_impl_(story_provider_impl),
      story_storage_(story_storage),
      story_scope_(story_provider_impl_->user_scope(),
                   kStoryScopeLabelPrefix + story_id_.get()),
      story_context_binding_(this) {
  auto story_scope = fuchsia::modular::StoryScope::New();
  story_scope->story_id = story_id;
  auto scope = fuchsia::modular::ComponentScope::New();
  scope->set_story_scope(std::move(*story_scope));
  story_provider_impl_->user_intelligence_provider()
      ->GetComponentIntelligenceServices(std::move(*scope),
                                         intelligence_services_.NewRequest());

  story_scope_.AddService<fuchsia::modular::ContextWriter>(
      [this](fidl::InterfaceRequest<fuchsia::modular::ContextWriter> request) {
        intelligence_services_->GetContextWriter(std::move(request));
      });
}

StoryControllerImpl::~StoryControllerImpl() = default;

void StoryControllerImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::StoryController> request) {
  bindings_.AddBinding(this, std::move(request));
}

bool StoryControllerImpl::IsRunning() {
  switch (state_) {
    case fuchsia::modular::StoryState::RUNNING:
      return true;
    case fuchsia::modular::StoryState::STOPPED:
      return false;
  }
}

void StoryControllerImpl::StopForDelete(const StopCallback& done) {
  operation_queue_.Add(new DeleteCall(this, done));
}

void StoryControllerImpl::StopForTeardown(const StopCallback& done) {
  operation_queue_.Add(new StopCall(this, false /* notify */, done));
}

fuchsia::modular::StoryState StoryControllerImpl::GetStoryState() const {
  return state_;
}

void StoryControllerImpl::Sync(const std::function<void()>& done) {
  operation_queue_.Add(new SyncCall(done));
}

void StoryControllerImpl::FocusModule(
    const fidl::VectorPtr<fidl::StringPtr>& module_path) {
  operation_queue_.Add(new FocusCall(this, module_path.Clone()));
}

void StoryControllerImpl::DefocusModule(
    const fidl::VectorPtr<fidl::StringPtr>& module_path) {
  operation_queue_.Add(new DefocusCall(this, module_path.Clone()));
}

void StoryControllerImpl::StopModule(
    const fidl::VectorPtr<fidl::StringPtr>& module_path,
    const std::function<void()>& done) {
  operation_queue_.Add(
      new StopModuleCall(this, story_storage_, module_path, done));
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
  pending_views_.erase(PathString(fit->module_data->module_path));
  running_mod_infos_.erase(fit);
}

fidl::StringPtr StoryControllerImpl::GetStoryId() const { return story_id_; }

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

  // TODO: remove this. MI4-1084
  if (active_links.count(*link_path) == 0) {
    // This is a new link: notify watchers.
    for (auto& i : links_watchers_.ptrs()) {
      LinkPath link_path_clone;
      link_path->Clone(&link_path_clone);
      (*i)->OnNewLink(std::move(link_path_clone));
    }
  }
}

fuchsia::modular::LinkPathPtr StoryControllerImpl::GetLinkPathForParameterName(
    const fidl::VectorPtr<fidl::StringPtr>& module_path, fidl::StringPtr name) {
  auto mod_info = FindRunningModInfo(module_path);
  // NOTE: |mod_info| will only be valid if the module at |module_path| is
  // running. Strictly speaking, this is unsafe. The source of truth is the
  // Ledger, accessible through StoryStorage, but the call would be dispatcher,
  // which would change the flow of all clients of this method. For now, we
  // leave as-is.
  FXL_DCHECK(mod_info) << PathString(module_path);

  const auto& param_map = mod_info->module_data->parameter_map;
  auto it = std::find_if(
      param_map.entries->begin(), param_map.entries->end(),
      [&name](const fuchsia::modular::ModuleParameterMapEntry& data) {
        return data.name == name;
      });

  fuchsia::modular::LinkPathPtr link_path = nullptr;
  if (it != param_map.entries->end()) {
    link_path = CloneOptional(it->link_path);
  }

  if (!link_path) {
    link_path = fuchsia::modular::LinkPath::New();
    link_path->module_path = module_path.Clone();
    link_path->link_name = name;
  }

  return link_path;
}

void StoryControllerImpl::EmbedModule(
    const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
    fidl::StringPtr module_name, fuchsia::modular::IntentPtr intent,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController>
        module_controller_request,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request,
    fuchsia::modular::ModuleSource module_source,
    std::function<void(fuchsia::modular::StartModuleStatus)> callback) {
  operation_queue_.Add(new AddIntentCall(
      this, parent_module_path.Clone(), module_name, std::move(intent),
      std::move(module_controller_request), nullptr /* surface_relation */,
      std::move(view_owner_request), std::move(module_source),
      std::move(callback)));
}

void StoryControllerImpl::StartModule(
    const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
    fidl::StringPtr module_name, fuchsia::modular::IntentPtr intent,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController>
        module_controller_request,
    fuchsia::modular::SurfaceRelationPtr surface_relation,
    fuchsia::modular::ModuleSource module_source,
    std::function<void(fuchsia::modular::StartModuleStatus)> callback) {
  operation_queue_.Add(new AddIntentCall(
      this, parent_module_path.Clone(), module_name, std::move(intent),
      std::move(module_controller_request), std::move(surface_relation),
      nullptr /* view_owner_request */, std::move(module_source),
      std::move(callback)));
}

void StoryControllerImpl::StartContainerInShell(
    const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
    fidl::StringPtr name, fuchsia::modular::SurfaceRelationPtr parent_relation,
    fidl::VectorPtr<fuchsia::modular::ContainerLayout> layout,
    fidl::VectorPtr<fuchsia::modular::ContainerRelationEntry> relationships,
    fidl::VectorPtr<fuchsia::modular::ContainerNodePtr> nodes) {
  operation_queue_.Add(new StartContainerInShellCall(
      this, parent_module_path.Clone(), name, std::move(parent_relation),
      std::move(layout), std::move(relationships), std::move(nodes)));
}

void StoryControllerImpl::ProcessPendingViews() {
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

  for (auto& kv : pending_views_) {
    auto* const running_mod_info = FindRunningModInfo(kv.second.module_path);
    if (!running_mod_info) {
      continue;
    }

    auto* const anchor = FindAnchor(running_mod_info);
    if (!anchor) {
      continue;
    }

    const auto anchor_view_id = PathString(anchor->module_data->module_path);
    if (!connected_views_.count(anchor_view_id)) {
      continue;
    }

    const auto view_id = PathString(kv.second.module_path);
    story_shell_->AddView(std::move(kv.second.view_owner), view_id,
                          anchor_view_id, std::move(kv.second.surface_relation),
                          std::move(kv.second.module_manifest));
    connected_views_.emplace(view_id);

    added_keys.push_back(kv.first);
  }

  if (added_keys.size()) {
    for (auto& key : added_keys) {
      pending_views_.erase(key);
    }
    ProcessPendingViews();
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
  // Control reaching here means that this update came from a remote device.
  operation_queue_.Add(
      new OnModuleDataUpdatedCall(this, std::move(module_data)));
}

void StoryControllerImpl::GetInfo(GetInfoCallback callback) {
  // Synced such that if GetInfo() is called after Start() or Stop(), the
  // state after the previously invoked operation is returned.
  //
  // If this call enters a race with a StoryProvider.DeleteStory() call, it
  // may silently not return or return null, or return the story info before
  // it was deleted, depending on where it gets sequenced in the operation
  // queues of StoryControllerImpl and StoryProviderImpl. The queues do not
  // block each other, however, because the call on the second queue is made
  // in the done callback of the operation on the first queue.
  //
  // This race is normal fidl concurrency behavior.
  operation_queue_.Add(new SyncCall([this, callback] {
    story_provider_impl_->GetStoryInfo(
        story_id_,
        // We capture only |state_| and not |this| because (1) we want the
        // state after SyncCall finishes, not after GetStoryInfo returns (i.e.
        // we want the state after the previous operation before GetInfo(),
        // but not after the operation following GetInfo()), and (2) |this|
        // may have been deleted when GetStoryInfo returned if there was a
        // Delete operation in the queue before GetStoryInfo().
        [state = state_, callback](fuchsia::modular::StoryInfoPtr story_info) {
          callback(std::move(*story_info), state);
        });
  }));
}

void StoryControllerImpl::Start(
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> request) {
  operation_queue_.Add(new StartCall(this, story_storage_, std::move(request)));
}

void StoryControllerImpl::Stop(StopCallback done) {
  operation_queue_.Add(new StopCall(this, true /* notify */, done));
}

void StoryControllerImpl::Watch(
    fidl::InterfaceHandle<fuchsia::modular::StoryWatcher> watcher) {
  auto ptr = watcher.Bind();
  ptr->OnStateChange(state_);
  watchers_.AddInterfacePtr(std::move(ptr));
}

void StoryControllerImpl::GetActiveModules(
    fidl::InterfaceHandle<fuchsia::modular::StoryModulesWatcher> watcher,
    GetActiveModulesCallback callback) {
  // We execute this in a SyncCall so that we are sure we don't fall in a
  // crack between a module being created and inserted in the connections
  // collection during some Operation.
  operation_queue_.Add(new SyncCall(fxl::MakeCopyable(
      [this, watcher = std::move(watcher), callback]() mutable {
        if (watcher) {
          auto ptr = watcher.Bind();
          modules_watchers_.AddInterfacePtr(std::move(ptr));
        }

        fidl::VectorPtr<fuchsia::modular::ModuleData> result;
        result.resize(running_mod_infos_.size());
        for (size_t i = 0; i < running_mod_infos_.size(); i++) {
          running_mod_infos_[i].module_data->Clone(&result->at(i));
        }
        callback(std::move(result));
      })));
}

void StoryControllerImpl::GetModules(GetModulesCallback callback) {
  auto on_run = Future<>::Create("StoryControllerImpl.GetModules.on_run");
  auto done =
      on_run->AsyncMap([this] { return story_storage_->ReadAllModuleData(); });
  operation_queue_.Add(WrapFutureAsOperation(
      "StoryControllerImpl.GetModules.op", on_run, done, callback));
}

void StoryControllerImpl::GetModuleController(
    fidl::VectorPtr<fidl::StringPtr> module_path,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController> request) {
  operation_queue_.Add(new SyncCall(
      fxl::MakeCopyable([this, module_path = std::move(module_path),
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
      })));
}

void StoryControllerImpl::GetActiveLinks(
    fidl::InterfaceHandle<fuchsia::modular::StoryLinksWatcher> watcher,
    GetActiveLinksCallback callback) {
  auto result = fidl::VectorPtr<fuchsia::modular::LinkPath>::New(0);

  std::set<fuchsia::modular::LinkPath> active_links = GetActiveLinksInternal();
  for (auto& p : active_links) {
    LinkPath clone;
    p.Clone(&clone);
    result.push_back(std::move(clone));
  }

  if (watcher) {
    links_watchers_.AddInterfacePtr(watcher.Bind());
  }
  callback(std::move(result));
}

void StoryControllerImpl::GetLink(
    fuchsia::modular::LinkPath link_path,
    fidl::InterfaceRequest<fuchsia::modular::Link> request) {
  ConnectLinkPath(fidl::MakeOptional(std::move(link_path)), std::move(request));
}

void StoryControllerImpl::AddModule(
    fidl::VectorPtr<fidl::StringPtr> parent_module_path,
    fidl::StringPtr module_name, fuchsia::modular::Intent intent,
    fuchsia::modular::SurfaceRelationPtr surface_relation) {
  if (!module_name || module_name->empty()) {
    // TODO(thatguy): When we report errors, make this an error reported back
    // to the client.
    FXL_LOG(FATAL) << "fuchsia::modular::StoryController::fuchsia::modular::"
                      "AddModule(): module_name must not be empty.";
  }

  // AddModule() only adds modules to the story shell. Internally, we use a
  // null SurfaceRelation to mean that the module is embedded, and a non-null
  // SurfaceRelation to indicate that the module is composed by the story
  // shell. If it is null, we set it to the default SurfaceRelation.
  if (!surface_relation) {
    surface_relation = fuchsia::modular::SurfaceRelation::New();
  }

  operation_queue_.Add(new AddIntentCall(
      this, std::move(parent_module_path), module_name, CloneOptional(intent),
      nullptr /* module_controller_request */, std::move(surface_relation),
      nullptr /* view_owner_request */,
      fuchsia::modular::ModuleSource::EXTERNAL,
      [](fuchsia::modular::StartModuleStatus) {}));
}

void StoryControllerImpl::StartStoryShell(
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> request) {
  story_shell_app_ = story_provider_impl_->StartStoryShell(std::move(request));
  story_shell_app_->services().ConnectToService(story_shell_.NewRequest());
  story_shell_->Initialize(story_context_binding_.NewBinding());
}

void StoryControllerImpl::SetState(
    const fuchsia::modular::StoryState new_state) {
  if (new_state == state_) {
    return;
  }

  state_ = new_state;

  for (auto& i : watchers_.ptrs()) {
    (*i)->OnStateChange(state_);
  }

  story_provider_impl_->NotifyStoryStateChange(story_id_, state_);
}

bool StoryControllerImpl::IsExternalModule(
    const fidl::VectorPtr<fidl::StringPtr>& module_path) {
  auto* const i = FindRunningModInfo(module_path);
  if (!i) {
    return false;
  }

  return i->module_data->module_source ==
         fuchsia::modular::ModuleSource::EXTERNAL;
}

StoryControllerImpl::RunningModInfo* StoryControllerImpl::FindRunningModInfo(
    const fidl::VectorPtr<fidl::StringPtr>& module_path) {
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

  // Traverse up until there is a non-embedded module. We recognize
  // non-embedded modules by having a non-null SurfaceRelation. If the root
  // module is there at all, it has a non-null surface relation.
  while (anchor && !anchor->module_data->surface_relation) {
    anchor =
        FindRunningModInfo(ParentModulePath(anchor->module_data->module_path));
  }

  return anchor;
}

void StoryControllerImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  story_provider_impl_->GetPresentation(story_id_, std::move(request));
}

void StoryControllerImpl::WatchVisualState(
    fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher) {
  story_provider_impl_->WatchVisualState(story_id_, std::move(watcher));
}

void StoryControllerImpl::Active() { story_provider_impl_->Active(story_id_); }

void StoryControllerImpl::HandleModuleDone(
    const fidl::VectorPtr<fidl::StringPtr>& module_path) {
  operation_queue_.Add(
      new StopModuleAndStoryIfEmptyCall(this, module_path, [] {}));
}

}  // namespace modular
