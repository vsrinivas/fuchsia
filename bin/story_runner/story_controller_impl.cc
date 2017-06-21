// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_controller_impl.h"

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/services/module/module_context.fidl.h"
#include "apps/modular/services/module/module_data.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/story_marker.fidl.h"
#include "apps/modular/src/story_runner/link_impl.h"
#include "apps/modular/src/story_runner/module_context_impl.h"
#include "apps/modular/src/story_runner/module_controller_impl.h"
#include "apps/modular/src/story_runner/story_provider_impl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/join_strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

constexpr char kStoryScopeLabelPrefix[] = "story-";

namespace {

fidl::String PathString(const fidl::Array<fidl::String>& module_path) {
  return ftl::JoinStrings(module_path, ":");
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
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryMarkerImpl);
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
      : Operation(container, done),
        story_controller_impl_(story_controller_impl),
        parent_module_path_(std::move(parent_module_path)),
        module_name_(module_name),
        module_url_(module_url),
        link_name_(link_name),
        surface_relation_(std::move(surface_relation)) {
    Ready();
  }

 private:
  void Run() {
    FlowToken flow{this};

    auto module_path = parent_module_path_.Clone();
    module_path.push_back(module_name_);
    auto link_path = LinkPath::New();
    link_path->module_path = parent_module_path_.Clone();
    link_path->link_name = link_name_;

    story_controller_impl_->story_storage_impl_->WriteModuleData(
        module_path, module_url_, link_path, ModuleSource::EXTERNAL,
        surface_relation_,
        [this, flow] {
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

  FTL_DISALLOW_COPY_AND_ASSIGN(AddModuleCall);
};

class StoryControllerImpl::GetModulesCall
    : Operation<fidl::Array<ModuleDataPtr>> {
 public:
  GetModulesCall(OperationContainer* const container,
                 StoryControllerImpl* const story_controller_impl,
                 const ResultCall& callback)
      : Operation(container, callback),
        story_controller_impl_(story_controller_impl) {
    Ready();
  }

 private:
  void Run() {
    FlowToken flow(this, &result_);

    story_controller_impl_->story_storage_impl_->ReadAllModuleData(
        [this, flow](fidl::Array<ModuleDataPtr> module_data) {
          result_ = std::move(module_data);
        });
  }

  StoryControllerImpl* const story_controller_impl_;
  fidl::Array<ModuleDataPtr> result_;
  FTL_DISALLOW_COPY_AND_ASSIGN(GetModulesCall);
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
      : Operation(container, done),
        story_controller_impl_(story_controller_impl),
        module_name_(module_name),
        module_url_(module_url),
        link_name_(link_name),
        link_json_(link_json) {
    Ready();
  }

 private:
  void Run() {
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
      auto link_path = LinkPath::New();
      link_path->module_path = fidl::Array<fidl::String>::New(0);
      link_path->link_name = link_name_;
      story_controller_impl_->GetLinkPath(link_path, link_.NewRequest());
      link_->UpdateObject(nullptr, link_json_);
      link_->Sync([flow] {});
    }

    auto module_path = fidl::Array<fidl::String>::New(1);
    module_path[0] = module_name_;

    new AddModuleCall(&operation_collection_, story_controller_impl_,
                      fidl::Array<fidl::String>::New(0), module_name_,
                      module_url_, link_name_, SurfaceRelation::New(), [flow] {});
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const fidl::String module_name_;
  const fidl::String module_url_;
  const fidl::String link_name_;
  const fidl::String link_json_;

  LinkPtr link_;

  OperationCollection operation_collection_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AddForCreateCall);
};

class StoryControllerImpl::StartCall : Operation<> {
 public:
  StartCall(OperationContainer* const container,
            StoryControllerImpl* const story_controller_impl,
            fidl::InterfaceRequest<mozart::ViewOwner> request)
      : Operation(container, [] {}),
        story_controller_impl_(story_controller_impl),
        request_(std::move(request)) {
    Ready();
  }

 private:
  void Run() {
    FlowToken flow{this};

    // If the story is running, we do nothing and close the view owner request.
    if (story_controller_impl_->IsRunning()) {
      FTL_LOG(INFO)
          << "StoryControllerImpl::StartCall() while already running: ignored.";
      return;
    }

    story_controller_impl_->StartStoryShell(std::move(request_));

    // Start *all* the root modules, not just the first one, with their
    // respective links.
    story_controller_impl_->story_storage_impl_->ReadAllModuleData(
        [this, flow](fidl::Array<ModuleDataPtr> data) {
          for (auto& module_data : data) {
            if (module_data->module_source == ModuleSource::EXTERNAL) {
              auto parent_path = module_data->module_path.Clone();
              parent_path.resize(parent_path.size() - 1);
              story_controller_impl_->StartModuleInShell(
                  std::move(parent_path),
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

  FTL_DISALLOW_COPY_AND_ASSIGN(StartCall);
};

class StoryControllerImpl::StopCall : Operation<> {
 public:
  StopCall(OperationContainer* const container,
           StoryControllerImpl* const story_controller_impl,
           std::function<void()> done)
      : Operation(container, done),
        story_controller_impl_(story_controller_impl) {
    Ready();
  }

 private:
  // StopCall may be run even on a story impl that is not running.
  void Run() {
    // At this point, we don't need to monitor the external modules for state
    // changes anymore, because the next state change of the story is triggered
    // by the Stop() call below.
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
      story_controller_impl_->story_shell_->Terminate(
          [this] { StoryShellDown(); });
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

    story_controller_impl_->NotifyStateChange();

    Done();
  };

  StoryControllerImpl* const story_controller_impl_;  // not owned
  int connections_count_{};
  int links_count_{};

  FTL_DISALLOW_COPY_AND_ASSIGN(StopCall);
};

class StoryControllerImpl::DeleteCall : Operation<> {
 public:
  DeleteCall(OperationContainer* const container,
             StoryControllerImpl* const story_controller_impl,
             std::function<void()> done)
      : Operation(container, [] {}),
        story_controller_impl_(story_controller_impl),
        done_(std::move(done)) {
    Ready();
  }

 private:
  void Run() {
    // No call to Done(), in order to block all further operations on the queue
    // until the instance is deleted.
    new StopCall(&operation_queue_, story_controller_impl_, done_);
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned

  // Not the result call of the Operation, because it's invoked without
  // unblocking the operation queue, to prevent subsequent operations from
  // executing until the instance is deleted, which cancels those operations.
  std::function<void()> done_;

  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeleteCall);
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
      : Operation(container, [] {}),
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
    FTL_DCHECK(!parent_module_path_.is_null());

    Ready();
  }

 private:
  void Run() {
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
          surface_relation_.Clone(),
          [this, flow] { Cont(flow); });

    } else {
      // If the link name is null, this module receives the default link of its
      // parent module. We need to retrieve which one it is from story storage.
      story_controller_impl_->story_storage_impl_->ReadModuleData(
          parent_module_path_, [this, flow](ModuleDataPtr module_data) {
            FTL_DCHECK(module_data);
            link_path_ = module_data->link_path.Clone();
            story_controller_impl_->story_storage_impl_->WriteModuleData(
                module_path_, module_url_, link_path_, module_source_,
                surface_relation_.Clone(),
                [this, flow] { Cont(flow); });
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

  void Launch(FlowToken flow) {
    auto launch_info = app::ApplicationLaunchInfo::New();

    app::ServiceProviderPtr app_services;
    launch_info->services = app_services.NewRequest();
    launch_info->url = module_url_;

    FTL_LOG(INFO) << "StoryControllerImpl::StartModule() " << module_url_;

    app::ApplicationControllerPtr application_controller;
    story_controller_impl_->story_scope_.GetLauncher()->CreateApplication(
        std::move(launch_info), application_controller.NewRequest());

    mozart::ViewProviderPtr view_provider;
    ConnectToService(app_services.get(), view_provider.NewRequest());
    view_provider->CreateView(std::move(view_owner_request_), nullptr);

    ModulePtr module;
    ConnectToService(app_services.get(), module.NewRequest());

    fidl::InterfaceHandle<ModuleContext> self;
    fidl::InterfaceRequest<ModuleContext> self_request = self.NewRequest();

    module->Initialize(std::move(self), std::move(outgoing_services_),
                       std::move(incoming_services_));

    Connection connection;

    connection.module_controller_impl.reset(new ModuleControllerImpl(
        story_controller_impl_, std::move(application_controller),
        std::move(module), module_path_));
    connection.module_controller_impl->Connect(
        std::move(module_controller_request_));

    ModuleContextInfo module_context_info = {
        story_controller_impl_->story_provider_impl_->component_context_info(),
        story_controller_impl_,
        story_controller_impl_->story_provider_impl_
            ->user_intelligence_provider()};

    connection.module_context_impl.reset(new ModuleContextImpl(
        module_path_, module_context_info, module_url_, link_path_,
        connection.module_controller_impl.get(), std::move(self_request)));

    story_controller_impl_->connections_.emplace_back(std::move(connection));

    NotifyWatchers();
  }

  void NotifyWatchers() {
    ModuleDataPtr module_data = ModuleData::New();
    module_data->module_url = module_url_;
    module_data->module_path = module_path_.Clone();
    module_data->link_path = link_path_.Clone();
    module_data->surface_relation = surface_relation_.Clone();

    story_controller_impl_->watchers_.ForAllPtrs(
        [&module_data](StoryWatcher* const watcher) {
          watcher->OnModuleAdded(module_data.Clone());
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

  FTL_DISALLOW_COPY_AND_ASSIGN(StartModuleCall);
};

class StoryControllerImpl::GetImportanceCall : Operation<float> {
 public:
  GetImportanceCall(OperationContainer* const container,
                    StoryControllerImpl* const story_controller_impl,
                    const ContextState& context_state,
                    ResultCall result_call)
      : Operation(container, std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        context_state_(context_state.Clone()) {
    Ready();
  }

 private:
  void Run() {
    FlowToken flow{this, &result_};

    story_controller_impl_->story_storage_impl_->ReadLog(
        [this, flow](fidl::Array<StoryContextLogPtr> log) {
          log_ = std::move(log);
          Cont(flow);
        });
  }

  void Cont(FlowToken flow) {
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

    result_ = score / count;
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const ContextState context_state_;
  fidl::Array<StoryContextLogPtr> log_;

  float result_{0.0};

  FTL_DISALLOW_COPY_AND_ASSIGN(GetImportanceCall);
};

class StoryControllerImpl::ModuleWatcherImpl : ModuleWatcher {
 public:
  ModuleWatcherImpl(fidl::InterfaceRequest<ModuleWatcher> request,
                    StoryControllerImpl* const story_controller_impl,
                    fidl::String module_id)
      : binding_(this, std::move(request)),
        story_controller_impl_(story_controller_impl),
        module_id_(std::move(module_id)) {}

 private:
  // |ModuleWatcher|
  void OnStateChange(ModuleState state) override {
    if (module_id_ == kRootModuleName) {
      story_controller_impl_->OnRootStateChange(state);
    }

    if (state == ModuleState::DONE) {
      if (story_controller_impl_->story_shell_) {
        story_controller_impl_->story_shell_->DefocusView(module_id_, [this] {
          // It's possible that |StopCall| has been called by the time this
          // callback start to execute.
          // TODO(alhaad): We need to persist a STOPPED bit for modules that
          // we stopped even after |StopForTeardown|.
          if (story_controller_impl_->external_modules_.empty()) {
            return;
          }
          StopModule();
        });
      } else {
        StopModule();
      }
    }
  }

  void StopModule() {
    auto it = std::find_if(story_controller_impl_->external_modules_.begin(),
                           story_controller_impl_->external_modules_.end(),
                           [this](const ExternalModule& m) {
                             return m.module_watcher_impl.get() == this;
                           });
    auto module_controller = std::move(it->module_controller);
    story_controller_impl_->external_modules_.erase(it);

    module_controller->Stop([] {
      // HACK(alhaad): This never gets called because |module_controller| goes
      // out of scope.
    });
  }

  fidl::Binding<ModuleWatcher> binding_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  const fidl::String module_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModuleWatcherImpl);
};

StoryControllerImpl::StoryControllerImpl(
    const fidl::String& story_id,
    ledger::PagePtr story_page,
    StoryProviderImpl* const story_provider_impl)
    : story_id_(story_id),
      story_provider_impl_(story_provider_impl),
      story_page_(std::move(story_page)),
      story_storage_impl_(new StoryStorageImpl(story_page_.get())),
      story_scope_(story_provider_impl_->user_scope(),
                   kStoryScopeLabelPrefix + story_id_.get()),
      story_context_binding_(this),
      story_marker_impl_(new StoryMarkerImpl) {
  story_scope_.AddService<StoryMarker>(
      [this](fidl::InterfaceRequest<StoryMarker> request) {
        story_marker_impl_->Connect(std::move(request));
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
  new StopCall(&operation_queue_, this, done);
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
    if (module_path.size() > 0) {
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

void StoryControllerImpl::ReleaseModule(
    ModuleControllerImpl* const module_controller_impl) {
  auto f = std::find_if(connections_.begin(), connections_.end(),
                        [module_controller_impl](const Connection& c) {
                          return c.module_controller_impl.get() ==
                                 module_controller_impl;
                        });
  FTL_DCHECK(f != connections_.end());
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
void StoryControllerImpl::GetLinkPath(const LinkPathPtr& link_path,
                                      fidl::InterfaceRequest<Link> request) {
  auto i = std::find_if(links_.begin(), links_.end(),
                        [&link_path](const std::unique_ptr<LinkImpl>& l) {
                          return l->link_path().Equals(link_path);
                        });
  if (i != links_.end()) {
    (*i)->Connect(std::move(request));
    return;
  }

  auto* const link_impl = new LinkImpl(story_storage_impl_.get(), link_path);
  link_impl->Connect(std::move(request));
  links_.emplace_back(link_impl);
  link_impl->set_orphaned_handler(
      [this, link_impl] { DisposeLink(link_impl); });
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
      std::move(outgoing_services),
      std::move(incoming_services), std::move(module_controller_request),
      std::move(view_owner_request));
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
    FTL_DCHECK(!module_controller_request.is_pending());
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
      std::move(outgoing_services),
      std::move(incoming_services), std::move(module_controller_request),
      view_owner.NewRequest());

  const fidl::String id = PathString(module_path);

  // If this is called during Stop(), story_shell_ might already have been
  // reset. TODO(mesch): Then the whole operation should fail.
  if (story_shell_) {
    // TODO(alhaad): When this piece of code gets run as a result of story
    // re-inflation, it is possible that module |id| gets connected before
    // module |parent_id|, which crashes story shell. This does not currently
    // happen by coincidence.
    fidl::String parent_id = PathString(parent_module_path);
    story_shell_->ConnectView(std::move(view_owner), id, parent_id,
                              std::move(surface_relation));
    if (focus) {
      story_shell_->FocusView(id, parent_id);
    }
  }

  if (module_source == ModuleSource::EXTERNAL) {
    TakeOwnership(std::move(module_controller), std::move(id));
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
  new StopCall(&operation_queue_, this, done);
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
  new AddModuleCall(&operation_queue_, this, std::move(module_path),
                    module_name, module_url, link_name,
                    std::move(surface_relation), [] {});
}

// |StoryController|
void StoryControllerImpl::GetModules(const GetModulesCallback& callback) {
  new GetModulesCall(&operation_queue_, this, callback);
}

// |StoryController|
void StoryControllerImpl::GetLink(fidl::Array<fidl::String> module_path,
                                  const fidl::String& name,
                                  fidl::InterfaceRequest<Link> request) {
  auto link_path = LinkPath::New();
  link_path->module_path = std::move(module_path);
  link_path->link_name = name;
  GetLinkPath(std::move(link_path), std::move(request));
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
  FTL_DCHECK(f != links_.end());
  links_.erase(f);
}

void StoryControllerImpl::TakeOwnership(ModuleControllerPtr module_controller,
                                        fidl::String id) {
  ModuleWatcherPtr watcher;
  auto module_watcher_impl = std::make_unique<ModuleWatcherImpl>(
      watcher.NewRequest(), this, std::move(id));
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
