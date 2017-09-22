// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/story_controller_impl.h"

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/ledger/storage.h"
#include "lib/module/fidl/module_context.fidl.h"
#include "lib/module/fidl/module_data.fidl.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/story/fidl/story_marker.fidl.h"
#include "peridot/bin/story_runner/link_impl.h"
#include "peridot/bin/story_runner/module_context_impl.h"
#include "peridot/bin/story_runner/module_controller_impl.h"
#include "peridot/bin/story_runner/story_provider_impl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/join_strings.h"
#include "lib/fsl/tasks/message_loop.h"

namespace modular {

constexpr char kStoryScopeLabelPrefix[] = "story-";

namespace {

fidl::String PathString(const fidl::Array<fidl::String>& module_path) {
  return fxl::JoinStrings(module_path, ":");
}

}  // namespace

class StoryControllerImpl::StoryMarkerImpl : StoryMarker {
 public:
  StoryMarkerImpl() = default;
  ~StoryMarkerImpl() override = default;

  void Connect(fidl::InterfaceRequest<StoryMarker> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  fidl::BindingSet<StoryMarker> bindings_;
  FXL_DISALLOW_COPY_AND_ASSIGN(StoryMarkerImpl);
};

class StoryControllerImpl::ModuleWatcherImpl : ModuleWatcher {
 public:
  ModuleWatcherImpl(fidl::InterfaceRequest<ModuleWatcher> request,
                    StoryControllerImpl* const story_controller_impl,
                    const fidl::Array<fidl::String>& module_path)
      : binding_(this, std::move(request)),
        story_controller_impl_(story_controller_impl),
        module_path_(module_path.Clone()) {}

  const fidl::Array<fidl::String>& module_path() const { return module_path_; }

 private:
  // |ModuleWatcher|
  void OnStateChange(ModuleState state) override {
    if (module_path_.size() == 1 && module_path_[0] == kRootModuleName) {
      story_controller_impl_->OnRootStateChange(state);
    }

    if (state == ModuleState::DONE) {
      story_controller_impl_->StopModule(module_path_, [] {});
    }
  }

  fidl::Binding<ModuleWatcher> binding_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  const fidl::Array<fidl::String> module_path_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleWatcherImpl);
};

class StoryControllerImpl::AddModuleCall : Operation<> {
 public:
  AddModuleCall(OperationContainer* const container,
                StoryControllerImpl* const story_controller_impl,
                fidl::Array<fidl::String> parent_module_path,
                const fidl::String& module_name,
                const fidl::String& module_url,
                const fidl::String& link_name,
                SurfaceRelationPtr surface_relation,
                const ResultCall& done)
      : Operation("StoryControllerImpl::AddModuleCall",
                  container,
                  done,
                  module_url),
        story_controller_impl_(story_controller_impl),
        parent_module_path_(std::move(parent_module_path)),
        module_name_(module_name),
        module_url_(module_url),
        link_name_(link_name),
        surface_relation_(std::move(surface_relation)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    auto module_path = parent_module_path_.Clone();
    module_path.push_back(module_name_);
    auto link_path = LinkPath::New();
    link_path->module_path = parent_module_path_.Clone();
    link_path->link_name = link_name_;

    story_controller_impl_->story_storage_impl_->WriteModuleData(
        module_path, module_url_, link_path, ModuleSource::EXTERNAL,
        surface_relation_, false, [this, flow] {
          if (story_controller_impl_->IsRunning()) {
            story_controller_impl_->StartModuleInShell(
                parent_module_path_, module_name_, module_url_, link_name_,
                nullptr, nullptr, nullptr, std::move(surface_relation_), true,
                ModuleSource::EXTERNAL);
          }
        });
  };

  StoryControllerImpl* const story_controller_impl_;
  const fidl::Array<fidl::String> parent_module_path_;
  const fidl::String module_name_;
  const fidl::String module_url_;
  const fidl::String link_name_;
  SurfaceRelationPtr surface_relation_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AddModuleCall);
};

// TODO(mesch): Merge the StoryStorageImpl operations into the
// StoryControllerImpl operations. This Operation exists only to align the
// operation queues of StoryControllerImpl and StoryStorageImpl.
class StoryControllerImpl::GetModulesCall
    : Operation<fidl::Array<ModuleDataPtr>> {
 public:
  GetModulesCall(OperationContainer* const container,
                 StoryControllerImpl* const story_controller_impl,
                 const ResultCall& callback)
      : Operation("StoryControllerImpl::GetModulesCall", container, callback),
        story_controller_impl_(story_controller_impl) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow(this, &result_);

    story_controller_impl_->story_storage_impl_->ReadAllModuleData(
        [this, flow](fidl::Array<ModuleDataPtr> result) {
          result_ = std::move(result);
        });
  }

  StoryControllerImpl* const story_controller_impl_;
  fidl::Array<ModuleDataPtr> result_;
  FXL_DISALLOW_COPY_AND_ASSIGN(GetModulesCall);
};

class StoryControllerImpl::AddForCreateCall : Operation<> {
 public:
  AddForCreateCall(OperationContainer* const container,
                   StoryControllerImpl* const story_controller_impl,
                   const fidl::String& module_name,
                   const fidl::String& module_url,
                   const fidl::String& link_name,
                   const fidl::String& link_json,
                   const ResultCall& done)
      : Operation("StoryControllerImpl::AddForCreateCall",
                  container,
                  done,
                  module_url),
        story_controller_impl_(story_controller_impl),
        module_name_(module_name),
        module_url_(module_url),
        link_name_(link_name),
        link_json_(link_json) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    // This flow branches and then joins on all the branches completing, which
    // is just fine to track with a flow token. A callback like used below:
    //
    //  [flow] {}
    //
    // just calls Done() when the last copy of it completes.

    if (!link_json_.is_null()) {
      // There is no module path; this link exists outside the scope of a
      // module.
      LinkPathPtr link_path = LinkPath::New();
      link_path->module_path = fidl::Array<fidl::String>::New(0);
      link_path->link_name = link_name_;
      story_controller_impl_->ConnectLinkPath(std::move(link_path),
                                              link_.NewRequest());
      link_->UpdateObject(nullptr, link_json_);
      link_->Sync([flow] {});
    }

    new AddModuleCall(&operation_collection_, story_controller_impl_,
                      fidl::Array<fidl::String>::New(0), module_name_,
                      module_url_, link_name_, SurfaceRelation::New(),
                      [flow] {});
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const fidl::String module_name_;
  const fidl::String module_url_;
  const fidl::String link_name_;
  const fidl::String link_json_;

  LinkPtr link_;

  OperationCollection operation_collection_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AddForCreateCall);
};

class StoryControllerImpl::StartCall : Operation<> {
 public:
  StartCall(OperationContainer* const container,
            StoryControllerImpl* const story_controller_impl,
            fidl::InterfaceRequest<mozart::ViewOwner> request)
      : Operation("StoryControllerImpl::StartCall", container, [] {}),
        story_controller_impl_(story_controller_impl),
        request_(std::move(request)) {
    Ready();
  }

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

    // Start *all* the root modules, not just the first one, with their
    // respective links.
    story_controller_impl_->story_storage_impl_->ReadAllModuleData(
        [this, flow](fidl::Array<ModuleDataPtr> data) {
          for (auto& module_data : data) {
            if (module_data->module_source == ModuleSource::EXTERNAL &&
                !module_data->module_stopped) {
              auto parent_path = module_data->module_path.Clone();
              parent_path.resize(parent_path.size() - 1);
              story_controller_impl_->StartModuleInShell(
                  parent_path,
                  module_data->module_path[module_data->module_path.size() - 1],
                  module_data->module_url, module_data->link_path->link_name,
                  nullptr, nullptr, nullptr,
                  module_data->surface_relation.Clone(), true,
                  module_data->module_source);
            }
          }

          story_controller_impl_->state_ = StoryState::STARTING;
          story_controller_impl_->NotifyStateChange();
        });
  };

  StoryControllerImpl* const story_controller_impl_;  // not owned
  fidl::InterfaceRequest<mozart::ViewOwner> request_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StartCall);
};

class StoryControllerImpl::StopCall : Operation<> {
 public:
  StopCall(OperationContainer* const container,
           StoryControllerImpl* const story_controller_impl,
           const bool notify,
           std::function<void()> done)
      : Operation("StoryControllerImpl::StopCall", container, done),
        story_controller_impl_(story_controller_impl),
        notify_(notify) {
    Ready();
  }

 private:
  // StopCall may be run even on a story impl that is not running.
  void Run() override {
    // At this point, we don't need to monitor the external modules for state
    // changes anymore, because the next state change of the story is triggered
    // by the Cleanup() call below.
    story_controller_impl_->external_modules_.clear();

    // At this point, we don't need notifications from disconnected
    // Links anymore, as they will all be disposed soon anyway.
    for (auto& link : story_controller_impl_->links_) {
      link->set_orphaned_handler(nullptr);
    }

    // Tear down all connections with a ModuleController first, then the
    // links between them.
    connections_count_ = story_controller_impl_->connections_.size();

    if (connections_count_ == 0) {
      StopStoryShell();
    } else {
      for (auto& connection : story_controller_impl_->connections_) {
        connection.module_controller_impl->Teardown(
            [this] { ConnectionDown(); });
      }
    }
  }

  void ConnectionDown() {
    --connections_count_;
    if (connections_count_ > 0) {
      // Not the last call.
      return;
    }

    StopStoryShell();
  }

  void StopStoryShell() {
    // It StopCall runs on a story that's not running, there is no story shell.
    if (story_controller_impl_->story_shell_) {
      story_controller_impl_->story_shell_.set_connection_error_handler(
          [this] { StoryShellDown(); });
      story_controller_impl_->story_shell_->Terminate();
    } else {
      StoryShellDown();
    }
  }

  void StoryShellDown() {
    story_controller_impl_->story_shell_controller_.reset();
    story_controller_impl_->story_shell_.reset();
    if (story_controller_impl_->story_context_binding_.is_bound()) {
      // Close() dchecks if called while not bound.
      story_controller_impl_->story_context_binding_.Close();
    }
    StopLinks();
  }

  void StopLinks() {
    links_count_ = story_controller_impl_->links_.size();
    if (links_count_ == 0) {
      Cleanup();
      return;
    }

    // The links don't need to be written now, because they all were written
    // when they were last changed, but we need to wait for the last write
    // request to finish, which is done with the Sync() request below.
    //
    // TODO(mesch): We really only need to Sync() on story_storage_impl_.
    for (auto& link : story_controller_impl_->links_) {
      link->Sync([this] { LinkDown(); });
    }
  }

  void LinkDown() {
    --links_count_;
    if (links_count_ > 0) {
      // Not the last call.
      return;
    }

    Cleanup();
  }

  void Cleanup() {
    // Clear the remaining links and connections in case there are some left. At
    // this point, no DisposeLink() calls can arrive anymore.
    story_controller_impl_->links_.clear();
    story_controller_impl_->connections_.clear();

    story_controller_impl_->state_ = StoryState::STOPPED;

    // If this StopCall is part of a DeleteCall, then we don't notify story
    // state changes; the pertinent state change will be the delete notification
    // instead.
    if (notify_) {
      story_controller_impl_->NotifyStateChange();
    }

    Done();
  };

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const bool notify_;  // Whether to notify state change; false in DeleteCall.
  int connections_count_{};
  int links_count_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(StopCall);
};

class StoryControllerImpl::StopModuleCall : Operation<> {
 public:
  StopModuleCall(OperationContainer* const container,
                 StoryControllerImpl* const story_controller_impl,
                 const fidl::Array<fidl::String>& module_path,
                 const std::function<void()>& done)
      : Operation("StoryControllerImpl::StopModuleCall", container, done),
        story_controller_impl_(story_controller_impl),
        module_path_(module_path.Clone()) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    // Read the module data.
    story_controller_impl_->story_storage_impl_->ReadModuleData(
        module_path_, [this, flow](ModuleDataPtr data) {
          module_data_ = std::move(data);
          Cont1(flow);
        });
  }

  void Cont1(FlowToken flow) {
    // If the module is external, we also notify story shell about it going
    // away. An internal module is stopped by its parent module, and it's up to
    // the parent module to defocus it first. TODO(mesch): Why not always
    // defocus?
    if (story_controller_impl_->story_shell_ &&
        module_data_->module_source == ModuleSource::EXTERNAL) {
      story_controller_impl_->story_shell_->DefocusView(
          PathString(module_path_), [this, flow] { Cont2(flow); });
    } else {
      Cont2(flow);
    }
  }

  void Cont2(FlowToken flow) {
    // Write the module data back, with module_stopped = true, which is a
    // global state shared between machines to track when the module is
    // explicitly stopped.
    module_data_->module_stopped = true;
    story_controller_impl_->story_storage_impl_->WriteModuleData(
        module_data_->Clone(), [this, flow] { Cont3(flow); });
  }

  void Cont3(FlowToken flow) {
    // Discard the ModuleWatcher, if there is any (for external modules only).
    auto i = std::find_if(
        story_controller_impl_->external_modules_.begin(),
        story_controller_impl_->external_modules_.end(),
        [this](const ExternalModule& m) {
          return m.module_watcher_impl->module_path().Equals(module_path_);
        });
    if (i != story_controller_impl_->external_modules_.end()) {
      story_controller_impl_->external_modules_.erase(i);
    }

    // Teardown the module, which discards the module controller. A parent
    // module can call ModuleController.Stop() multiple times before the
    // ModuleController connection gets disconnected by Teardown(). Therefore,
    // this StopModuleCall Operation will cause the calls to be queued.
    // The first Stop() will cause the ModuleController to be closed, and
    // so subsequent Stop() attempts will not find a controller and will return.
    auto ii = std::find_if(
        story_controller_impl_->connections_.begin(),
        story_controller_impl_->connections_.end(),
        [this](const Connection& c) {
          return c.module_context_impl->module_path().Equals(module_path_);
        });

    if (ii == story_controller_impl_->connections_.end()) {
      FXL_LOG(INFO) << "No ModuleController for Module"
                    << " " << PathString(module_path_) << ". "
                    << "Was ModuleContext.Stop() called twice?";
      return;
    }

    ii->module_controller_impl->Teardown([this, flow] { Cont4(flow); });
  }

  void Cont4(FlowToken /*flow*/) {
    story_controller_impl_->modules_watchers_.ForAllPtrs(
        [this](StoryModulesWatcher* const watcher) {
          watcher->OnNewModule(module_data_.Clone());
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const fidl::Array<fidl::String> module_path_;
  ModuleDataPtr module_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StopModuleCall);
};

class StoryControllerImpl::DeleteCall : Operation<> {
 public:
  DeleteCall(OperationContainer* const container,
             StoryControllerImpl* const story_controller_impl,
             std::function<void()> done)
      : Operation("StoryControllerImpl::DeleteCall", container, [] {}),
        story_controller_impl_(story_controller_impl),
        done_(std::move(done)) {
    Ready();
  }

 private:
  void Run() override {
    // No call to Done(), in order to block all further operations on the queue
    // until the instance is deleted.
    new StopCall(&operation_queue_, story_controller_impl_, false /* notify */,
                 done_);
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned

  // Not the result call of the Operation, because it's invoked without
  // unblocking the operation queue, to prevent subsequent operations from
  // executing until the instance is deleted, which cancels those operations.
  std::function<void()> done_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteCall);
};

class StoryControllerImpl::StartModuleCall : Operation<> {
 public:
  StartModuleCall(
      OperationContainer* const container,
      StoryControllerImpl* const story_controller_impl,
      const fidl::Array<fidl::String>& parent_module_path,
      const fidl::Array<fidl::String>& module_path,
      const fidl::String& module_url,
      const fidl::String& link_name,
      const ModuleSource module_source,
      SurfaceRelationPtr surface_relation,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller_request,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : Operation("StoryControllerImpl::StartModuleCall",
                  container,
                  [] {},
                  module_url),
        story_controller_impl_(story_controller_impl),
        parent_module_path_(parent_module_path.Clone()),
        module_path_(module_path.Clone()),
        module_url_(module_url),
        link_name_(link_name),
        module_source_(module_source),
        surface_relation_(std::move(surface_relation)),
        outgoing_services_(std::move(outgoing_services)),
        incoming_services_(std::move(incoming_services)),
        module_controller_request_(std::move(module_controller_request)),
        view_owner_request_(std::move(view_owner_request)) {
    FXL_DCHECK(!parent_module_path_.is_null());

    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    // We currently require a 1:1 relationship between module
    // application instances and Module service instances, because
    // flutter only allows one ViewOwner per flutter application,
    // and we need one ViewOwner instance per Module instance.

    if (link_name_) {
      link_path_ = LinkPath::New();
      link_path_->module_path = parent_module_path_.Clone();
      link_path_->link_name = link_name_;

      story_controller_impl_->story_storage_impl_->WriteModuleData(
          module_path_, module_url_, link_path_, module_source_,
          surface_relation_.Clone(), false, [this, flow] { Cont(flow); });

    } else {
      // If the link name is null, this module receives the default link of its
      // parent module. We need to retrieve which one it is from story storage.
      story_controller_impl_->story_storage_impl_->ReadModuleData(
          parent_module_path_, [this, flow](ModuleDataPtr module_data) {
            FXL_DCHECK(module_data);
            link_path_ = module_data->link_path.Clone();
            story_controller_impl_->story_storage_impl_->WriteModuleData(
                module_path_, module_url_, link_path_, module_source_,
                surface_relation_.Clone(), false, [this, flow] { Cont(flow); });
          });
    }
  }

  void Cont(FlowToken flow) {
    // TODO(mesch): connections_ should be a map<>.
    auto i = std::find_if(
        story_controller_impl_->connections_.begin(),
        story_controller_impl_->connections_.end(),
        [this](const Connection& c) {
          return c.module_context_impl->module_path().Equals(module_path_);
        });

    // We launch the new module if it doesn't run yet.
    if (i == story_controller_impl_->connections_.end()) {
      Launch(flow);
      return;
    }

    // If the new module is already running, but with a different URL or on a
    // different link, or if a service exchange is requested, we tear it down
    // then launch a new module.
    //
    // TODO(mesch): If only the link is different, we should just hook the
    // existing module instance on a new link and notify it about the changed
    // link value.
    if (i->module_context_impl->module_url() != module_url_ ||
        !i->module_context_impl->link_path().Equals(*link_path_) ||
        outgoing_services_.is_valid() || incoming_services_.is_pending()) {
      i->module_controller_impl->Teardown([this, flow] {
        // NOTE(mesch): i is invalid at this point.
        Launch(flow);
      });
      return;
    }

    // If the module is already running on the same URL and link, we just
    // connect the module controller request.
    i->module_controller_impl->Connect(std::move(module_controller_request_));
  }

  void Launch(FlowToken /*flow*/) {
    FXL_LOG(INFO) << "StoryControllerImpl::StartModule() " << module_url_;
    auto module_config = AppConfig::New();
    module_config->url = module_url_;

    mozart::ViewProviderPtr view_provider;
    fidl::InterfaceRequest<mozart::ViewProvider> view_provider_request =
        view_provider.NewRequest();
    view_provider->CreateView(std::move(view_owner_request_), nullptr);

    fidl::InterfaceHandle<ModuleContext> self;
    fidl::InterfaceRequest<ModuleContext> self_request = self.NewRequest();

    Connection connection;
    connection.module_controller_impl = std::make_unique<ModuleControllerImpl>(
        story_controller_impl_,
        story_controller_impl_->story_scope_.GetLauncher(),
        std::move(module_config), module_path_, std::move(self),
        std::move(view_provider_request), std::move(outgoing_services_),
        std::move(incoming_services_));
    connection.module_controller_impl->Connect(
        std::move(module_controller_request_));

    ModuleContextInfo module_context_info = {
        story_controller_impl_->story_provider_impl_->component_context_info(),
        story_controller_impl_,
        story_controller_impl_->story_provider_impl_
            ->user_intelligence_provider()};

    module_data_ = ModuleData::New();
    module_data_->module_url = module_url_;
    module_data_->module_path = module_path_.Clone();
    module_data_->link_path = link_path_.Clone();
    module_data_->surface_relation = surface_relation_.Clone();

    connection.module_context_impl = std::make_unique<ModuleContextImpl>(
        module_context_info, module_data_.Clone(),
        connection.module_controller_impl.get(), std::move(self_request));

    story_controller_impl_->connections_.emplace_back(std::move(connection));

    NotifyWatchers();
  }

  void NotifyWatchers() {
    story_controller_impl_->watchers_.ForAllPtrs(
        [this](StoryWatcher* const watcher) {
          watcher->OnModuleAdded(module_data_.Clone());
        });

    story_controller_impl_->modules_watchers_.ForAllPtrs(
        [this](StoryModulesWatcher* const watcher) {
          watcher->OnNewModule(module_data_.Clone());
        });
  }

  // Passed in:
  StoryControllerImpl* const story_controller_impl_;  // not owned
  const fidl::Array<fidl::String> parent_module_path_;
  const fidl::Array<fidl::String> module_path_;
  const fidl::String module_url_;
  const fidl::String link_name_;
  const ModuleSource module_source_;
  const SurfaceRelationPtr surface_relation_;
  fidl::InterfaceHandle<app::ServiceProvider> outgoing_services_;
  fidl::InterfaceRequest<app::ServiceProvider> incoming_services_;
  fidl::InterfaceRequest<ModuleController> module_controller_request_;
  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;

  LinkPathPtr link_path_;
  ModuleDataPtr module_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StartModuleCall);
};

class StoryControllerImpl::GetImportanceCall : Operation<float> {
 public:
  GetImportanceCall(OperationContainer* const container,
                    StoryControllerImpl* const story_controller_impl,
                    const ContextState& context_state,
                    ResultCall result_call)
      : Operation("StoryControllerImpl::GetImportanceCall",
                  container,
                  std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        context_state_(context_state.Clone()) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &result_};

    story_controller_impl_->story_storage_impl_->ReadLog(
        [this, flow](fidl::Array<StoryContextLogPtr> log) {
          log_ = std::move(log);
          Cont(flow);
        });
  }

  void Cont(FlowToken /*flow*/) {
    // HACK(mesch): Hardcoded importance computation. Will be delegated
    // somewhere more flexible eventually.
    auto i = context_state_.find(kStoryImportanceContext);
    if (i == context_state_.cend()) {
      result_ = 1.0;
      return;
    }

    const auto& context_value = i.GetValue();

    float score = 0.0;
    float count = 0.0;

    for (auto& entry : log_) {
      auto i = entry->context.find(kStoryImportanceContext);
      if (i == entry->context.end()) {
        continue;
      }

      // Any log entry with context relevant to importance counts.
      count += 1.0;

      const auto& log_value = i.GetValue();
      if (context_value != log_value) {
        continue;
      }

      // Any log entry with context relevant to importance increases the
      // importance score.
      score += 1.0;
    }

    if (count > 0.0) {
      result_ = score / count;
    }
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const ContextState context_state_;
  fidl::Array<StoryContextLogPtr> log_;

  float result_{0.0};

  FXL_DISALLOW_COPY_AND_ASSIGN(GetImportanceCall);
};

StoryControllerImpl::StoryControllerImpl(
    const fidl::String& story_id,
    LedgerClient* const ledger_client,
    LedgerPageId story_page_id,
    StoryProviderImpl* const story_provider_impl)
    : story_id_(story_id),
      story_provider_impl_(story_provider_impl),
      story_storage_impl_(new StoryStorageImpl(
          ledger_client, std::move(story_page_id))),
      story_scope_(story_provider_impl_->user_scope(),
                   kStoryScopeLabelPrefix + story_id_.get()),
      story_context_binding_(this),
      story_marker_impl_(new StoryMarkerImpl) {
  story_scope_.AddService<StoryMarker>(
      [this](fidl::InterfaceRequest<StoryMarker> request) {
        story_marker_impl_->Connect(std::move(request));
      });

  auto story_scope = maxwell::StoryScope::New();
  story_scope->story_id = story_id;
  auto scope = maxwell::ComponentScope::New();
  scope->set_story_scope(std::move(story_scope));
  story_provider_impl_->user_intelligence_provider()
      ->GetComponentIntelligenceServices(std::move(scope),
                                         intelligence_services_.NewRequest());

  story_scope_.AddService<maxwell::ContextWriter>(
      [this](fidl::InterfaceRequest<maxwell::ContextWriter> request) {
        intelligence_services_->GetContextWriter(std::move(request));
      });
}

StoryControllerImpl::~StoryControllerImpl() = default;

void StoryControllerImpl::Connect(
    fidl::InterfaceRequest<StoryController> request) {
  bindings_.AddBinding(this, std::move(request));
}

bool StoryControllerImpl::IsRunning() {
  switch (state_) {
    case StoryState::STARTING:
    case StoryState::RUNNING:
    case StoryState::DONE:
      return true;
    case StoryState::INITIAL:
    case StoryState::STOPPED:
    case StoryState::ERROR:
      return false;
  }
}

void StoryControllerImpl::StopForDelete(const StopCallback& done) {
  new DeleteCall(&operation_queue_, this, done);
}

void StoryControllerImpl::StopForTeardown(const StopCallback& done) {
  new StopCall(&operation_queue_, this, false /* notify */, done);
}

void StoryControllerImpl::AddForCreate(const fidl::String& module_name,
                                       const fidl::String& module_url,
                                       const fidl::String& link_name,
                                       const fidl::String& link_json,
                                       const std::function<void()>& done) {
  new AddForCreateCall(&operation_queue_, this, module_name, module_url,
                       link_name, link_json, done);
}

StoryState StoryControllerImpl::GetStoryState() const {
  return state_;
}

void StoryControllerImpl::Log(StoryContextLogPtr log_entry) {
  story_storage_impl_->Log(std::move(log_entry));
}

void StoryControllerImpl::Sync(const std::function<void()>& done) {
  story_storage_impl_->Sync(done);
}

void StoryControllerImpl::GetImportance(
    const ContextState& context_state,
    const std::function<void(float)>& result) {
  new GetImportanceCall(&operation_queue_, this, context_state, result);
}

void StoryControllerImpl::FocusModule(
    const fidl::Array<fidl::String>& module_path) {
  if (story_shell_) {
    if (!module_path.empty()) {
      // Focus modules relative to their parent modules.
      fidl::Array<fidl::String> parent_module_path = module_path.Clone();
      parent_module_path.resize(parent_module_path.size() - 1);
      story_shell_->FocusView(PathString(module_path),
                              PathString(parent_module_path));
    } else {
      // Focus root modules absolutely.
      story_shell_->FocusView(PathString(module_path), nullptr);
    }
  }
}

void StoryControllerImpl::DefocusModule(
    const fidl::Array<fidl::String>& module_path) {
  if (story_shell_) {
    story_shell_->DefocusView(PathString(module_path), [] {});
  }
}

void StoryControllerImpl::StopModule(
    const fidl::Array<fidl::String>& module_path,
    const std::function<void()>& done) {
  new StopModuleCall(&operation_queue_, this, module_path, done);
}

void StoryControllerImpl::ReleaseModule(
    ModuleControllerImpl* const module_controller_impl) {
  auto f = std::find_if(connections_.begin(), connections_.end(),
                        [module_controller_impl](const Connection& c) {
                          return c.module_controller_impl.get() ==
                                 module_controller_impl;
                        });
  FXL_DCHECK(f != connections_.end());
  f->module_controller_impl.release();
  connections_.erase(f);
}

const fidl::String& StoryControllerImpl::GetStoryId() const {
  return story_id_;
}

void StoryControllerImpl::RequestStoryFocus() {
  story_provider_impl_->RequestStoryFocus(story_id_);
}

// TODO(vardhan): Should this operation be queued here, or in |LinkImpl|?
// Currently it is neither.
void StoryControllerImpl::ConnectLinkPath(
    LinkPathPtr link_path,
    fidl::InterfaceRequest<Link> request) {
  auto i = std::find_if(links_.begin(), links_.end(),
                        [&link_path](const std::unique_ptr<LinkImpl>& l) {
                          return l->link_path().Equals(link_path);
                        });
  if (i != links_.end()) {
    (*i)->Connect(std::move(request));
    return;
  }

  LinkImpl* const link_impl =
      new LinkImpl(story_storage_impl_.get(), std::move(link_path));
  link_impl->Connect(std::move(request));
  links_.emplace_back(link_impl);
  link_impl->set_orphaned_handler(
      [this, link_impl] { DisposeLink(link_impl); });

  links_watchers_.ForAllPtrs([link_impl](StoryLinksWatcher* const watcher) {
    watcher->OnNewLink(link_impl->link_path().Clone());
  });
}

void StoryControllerImpl::StartModule(
    const fidl::Array<fidl::String>& parent_module_path,
    const fidl::String& module_name,
    const fidl::String& module_url,
    const fidl::String& link_name,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller_request,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    const ModuleSource module_source) {
  fidl::Array<fidl::String> module_path = parent_module_path.Clone();
  module_path.push_back(module_name);

  new StartModuleCall(
      &operation_queue_, this, parent_module_path, module_path, module_url,
      link_name, module_source, SurfaceRelation::New(),
      std::move(outgoing_services), std::move(incoming_services),
      std::move(module_controller_request), std::move(view_owner_request));
}

void StoryControllerImpl::StartModuleInShell(
    const fidl::Array<fidl::String>& parent_module_path,
    const fidl::String& module_name,
    const fidl::String& module_url,
    const fidl::String& link_name,
    fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ModuleController> module_controller_request,
    SurfaceRelationPtr surface_relation,
    const bool focus,
    ModuleSource module_source) {
  ModuleControllerPtr module_controller;
  mozart::ViewOwnerPtr view_owner;

  if (module_source == ModuleSource::EXTERNAL) {
    FXL_DCHECK(!module_controller_request.is_pending());
    module_controller_request = module_controller.NewRequest();
  }

  fidl::Array<fidl::String> module_path = parent_module_path.Clone();
  module_path.push_back(module_name);

  // TODO(mesch): The StartModuleCall may result in just a new ModuleController
  // connection to an existing ModuleControllerImpl. In that case, the view
  // owner request is closed, and the view owner should not be sent to the story
  // shell.

  new StartModuleCall(
      &operation_queue_, this, parent_module_path, module_path, module_url,
      link_name, module_source, surface_relation.Clone(),
      std::move(outgoing_services), std::move(incoming_services),
      std::move(module_controller_request), view_owner.NewRequest());

  const fidl::String view_id = PathString(module_path);

  // If this is called during Stop(), story_shell_ might already have been
  // reset. TODO(mesch): Then the whole operation should fail.
  if (story_shell_) {
    // TODO(alhaad): When this piece of code gets run as a result of story
    // re-inflation, it is possible that module |id| gets connected before
    // module |parent_id|, which crashes story shell. This does not currently
    // happen by coincidence.
    fidl::String parent_view_id = PathString(parent_module_path);
    story_shell_->ConnectView(std::move(view_owner), view_id, parent_view_id,
                              std::move(surface_relation));
    if (focus) {
      story_shell_->FocusView(view_id, parent_view_id);
    }
  }

  if (module_source == ModuleSource::EXTERNAL) {
    AddModuleWatcher(std::move(module_controller), module_path);
  }
}

// |StoryController|
void StoryControllerImpl::GetInfo(const GetInfoCallback& callback) {
  // Synced such that if GetInfo() is called after Start() or Stop(), the state
  // after the previously invoked operation is returned.
  //
  // If this call enters a race with a StoryProvider.DeleteStory() call, it may
  // silently not return or return null, or return the story info before it was
  // deleted, depending on where it gets sequenced in the operation queues of
  // StoryControllerImpl and StoryProviderImpl. The queues do not block each
  // other, however, because the call on the second queue is made in the done
  // callback of the operation on the first queue.
  //
  // This race is normal fidl concurrency behavior.
  new SyncCall(&operation_queue_, [this, callback] {
    story_provider_impl_->GetStoryInfo(
        story_id_,
        // We capture only |state_| and not |this| because (1) we want the state
        // after SyncCall finishes, not after GetStoryInfo returns (i.e. we want
        // the state after the previous operation before GetInfo(), but not
        // after the operation following GetInfo()), and (2) |this| may have
        // been deleted when GetStoryInfo returned if there was a Delete
        // operation in the queue before GetStoryInfo().
        [ state = state_, callback ](modular::StoryInfoPtr story_info) {
          callback(std::move(story_info), state);
        });
  });
}

// |StoryController|
void StoryControllerImpl::SetInfoExtra(const fidl::String& name,
                                       const fidl::String& value,
                                       const SetInfoExtraCallback& callback) {
  story_provider_impl_->SetStoryInfoExtra(story_id_, name, value, callback);
}

// |StoryController|
void StoryControllerImpl::Start(
    fidl::InterfaceRequest<mozart::ViewOwner> request) {
  new StartCall(&operation_queue_, this, std::move(request));
}

// |StoryController|
void StoryControllerImpl::Stop(const StopCallback& done) {
  new StopCall(&operation_queue_, this, true /* notify */, done);
}

// |StoryController|
void StoryControllerImpl::Watch(fidl::InterfaceHandle<StoryWatcher> watcher) {
  auto ptr = StoryWatcherPtr::Create(std::move(watcher));
  ptr->OnStateChange(state_);
  watchers_.AddInterfacePtr(std::move(ptr));
}

// |StoryController|
void StoryControllerImpl::AddModule(fidl::Array<fidl::String> module_path,
                                    const fidl::String& module_name,
                                    const fidl::String& module_url,
                                    const fidl::String& link_name,
                                    SurfaceRelationPtr surface_relation) {
  // In the API, a null module path is allowed to represent the empty module
  // path.
  if (module_path.is_null()) {
    module_path.resize(0);
  }

  new AddModuleCall(&operation_queue_, this, std::move(module_path),
                    module_name, module_url, link_name,
                    std::move(surface_relation), [] {});
}

// |StoryController|
void StoryControllerImpl::GetActiveModules(
    fidl::InterfaceHandle<StoryModulesWatcher> watcher,
    const GetActiveModulesCallback& callback) {
  // We execute this in a SyncCall so that we are sure we don't fall in a crack
  // between a module being created and inserted in the connections collection
  // during some Operation.
  new SyncCall(&operation_queue_, fxl::MakeCopyable([
    this, watcher = std::move(watcher), callback
  ]() mutable {
    if (watcher) {
      auto ptr = StoryModulesWatcherPtr::Create(std::move(watcher));
      modules_watchers_.AddInterfacePtr(std::move(ptr));
    }

    fidl::Array<ModuleDataPtr> result;
    result.resize(0);
    for (auto& connection : connections_) {
      result.push_back(connection.module_context_impl->module_data().Clone());
    }
    callback(std::move(result));
  }));
}

// |StoryController|
void StoryControllerImpl::GetModules(const GetModulesCallback& callback) {
  new GetModulesCall(&operation_queue_, this, callback);
}

// |StoryController|
void StoryControllerImpl::GetModuleController(
    fidl::Array<fidl::String> module_path,
    fidl::InterfaceRequest<ModuleController> request) {
  for (auto& connection : connections_) {
    if (module_path.Equals(connection.module_context_impl->module_path())) {
      connection.module_controller_impl->Connect(std::move(request));
      return;
    }
  }

  // Trying to get a controller for a module that is not active just drops the
  // connection request.
}

// |StoryController|
void StoryControllerImpl::GetActiveLinks(
    fidl::InterfaceHandle<StoryLinksWatcher> watcher,
    const GetActiveLinksCallback& callback) {
  // We execute this in a SyncCall so that we are sure we don't fall in a crack
  // between a link being created and inserted in the links collection during
  // some Operation. (Right now Links are not created in an Operation, but we
  // don't want to rely on it.)
  new SyncCall(&operation_queue_, fxl::MakeCopyable([
    this, watcher = std::move(watcher), callback
  ]() mutable {
    if (watcher) {
      auto ptr = StoryLinksWatcherPtr::Create(std::move(watcher));
      links_watchers_.AddInterfacePtr(std::move(ptr));
    }

    // Only active links, i.e. links currently in use by a module, are
    // returned here. Eventually we might want to list all links, but this
    // requires some changes to how links are stored to make it
    // nice. (Right now we need to parse keys, which we don't want to.)
    fidl::Array<LinkPathPtr> result;
    result.resize(0);
    for (auto& link : links_) {
      result.push_back(link->link_path().Clone());
    }
    callback(std::move(result));
  }));
}

// |StoryController|
void StoryControllerImpl::GetLink(fidl::Array<fidl::String> module_path,
                                  const fidl::String& name,
                                  fidl::InterfaceRequest<Link> request) {
  // In the API, a null module path is allowed to represent the empty module
  // path.
  if (module_path.is_null()) {
    module_path.resize(0);
  }

  LinkPathPtr link_path = LinkPath::New();
  link_path->module_path = std::move(module_path);
  link_path->link_name = name;

  ConnectLinkPath(std::move(link_path), std::move(request));
}

void StoryControllerImpl::StartStoryShell(
    fidl::InterfaceRequest<mozart::ViewOwner> request) {
  story_shell_controller_ = story_provider_impl_->StartStoryShell(
      story_context_binding_.NewBinding(), story_shell_.NewRequest(),
      std::move(request));
}

void StoryControllerImpl::NotifyStateChange() {
  watchers_.ForAllPtrs(
      [this](StoryWatcher* const watcher) { watcher->OnStateChange(state_); });

  story_provider_impl_->NotifyStoryStateChange(story_id_, state_);

  // NOTE(mesch): This gets scheduled on the StoryProviderImpl Operation
  // queue. If the current StoryControllerImpl Operation is part of a
  // DeleteStory Operation of the StoryProviderImpl, then the SetStoryState
  // Operation gets scheduled after the delete of the story is completed, and it
  // will not write anything. The Operation on the other queue is not part of
  // this Operation, so not subject to locking if it travels in wrong direction
  // of the hierarchy (the principle we follow is that an Operation in one
  // container may sync on the operation queue of something inside the
  // container, but not something outside the container; this way we prevent
  // lock cycles).
  //
  // TODO(mesch): It would still be nicer if we could complete the State writing
  // while this Operation is executing so that it stays on our queue and there's
  // no race condition. We need our own copy of the Page* for that.

  story_storage_impl_->WriteDeviceData(
      story_id_, story_provider_impl_->device_id(), state_, [] {});
}

void StoryControllerImpl::DisposeLink(LinkImpl* const link) {
  auto f = std::find_if(
      links_.begin(), links_.end(),
      [link](const std::unique_ptr<LinkImpl>& l) { return l.get() == link; });
  FXL_DCHECK(f != links_.end());
  links_.erase(f);
}

void StoryControllerImpl::AddModuleWatcher(
    ModuleControllerPtr module_controller,
    const fidl::Array<fidl::String>& module_path) {
  ModuleWatcherPtr watcher;
  auto module_watcher_impl = std::make_unique<ModuleWatcherImpl>(
      watcher.NewRequest(), this, module_path);
  module_controller->Watch(std::move(watcher));
  external_modules_.emplace_back(ExternalModule{std::move(module_watcher_impl),
                                                std::move(module_controller)});
}

void StoryControllerImpl::OnRootStateChange(const ModuleState state) {
  switch (state) {
    case ModuleState::STARTING:
      state_ = StoryState::STARTING;
      break;
    case ModuleState::RUNNING:
    case ModuleState::UNLINKED:
      state_ = StoryState::RUNNING;
      break;
    case ModuleState::STOPPED:
      state_ = StoryState::STOPPED;
      break;
    case ModuleState::DONE:
      state_ = StoryState::DONE;
      break;
    case ModuleState::ERROR:
      state_ = StoryState::ERROR;
      break;
  }

  NotifyStateChange();
}

}  // namespace modular
