// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story_runner/story_provider_impl.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/time.h>

#include <memory>
#include <utility>
#include <vector>

#include "src/modular/bin/sessionmgr/agent_runner/agent_runner.h"
#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/storage/story_storage.h"
#include "src/modular/bin/sessionmgr/story_runner/story_controller_impl.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

class StoryProviderImpl::StopStoryCall : public Operation<> {
 public:
  using StoryRuntimesMap = std::map<std::string, struct StoryRuntimeContainer>;

  StopStoryCall(std::string story_id, const bool skip_notifying_sessionshell,
                StoryRuntimesMap* const story_runtime_containers, ResultCall result_call)
      : Operation("StoryProviderImpl::StopStoryCall", std::move(result_call)),
        story_id_(std::move(story_id)),
        skip_notifying_sessionshell_(skip_notifying_sessionshell),
        story_runtime_containers_(story_runtime_containers) {}

 private:
  void Run() override {
    FlowToken flow{this};

    auto i = story_runtime_containers_->find(story_id_);
    if (i == story_runtime_containers_->end()) {
      FX_LOGS(WARNING) << "I was told to stop story " << story_id_ << ", but I can't find it.";
      return;
    }

    FX_DCHECK(i->second.controller_impl != nullptr);
    i->second.controller_impl->Teardown(skip_notifying_sessionshell_,
                                        [weak_ptr = GetWeakPtr(), this, flow] {
                                          // Ensure |story_runtime_containers_| has not been
                                          // destroyed.
                                          //
                                          // This operation and its parent |StoryProviderImpl| may
                                          // be destroyed before this callback executes, for example
                                          // when |StoryProviderImpl.Teardown| times out. When this
                                          // happens, `operation_queue_` and this operation are
                                          // destroyed before |story_runtime_containers_|,
                                          // invalidating |weak_ptr|.
                                          if (!weak_ptr)
                                            return;
                                          story_runtime_containers_->erase(story_id_);
                                        });
  }

  const std::string story_id_;
  const bool skip_notifying_sessionshell_;
  StoryRuntimesMap* const story_runtime_containers_;
};

// Loads a StoryRuntimeContainer object and stores it in
// |story_provider_impl.story_runtime_containers_| so that the story is ready to be run.
class StoryProviderImpl::LoadStoryRuntimeCall : public Operation<StoryRuntimeContainer*> {
 public:
  LoadStoryRuntimeCall(StoryProviderImpl* const story_provider_impl,
                       SessionStorage* const session_storage, std::string story_id,
                       inspect::Node* root_node, ResultCall result_call)
      : Operation("StoryProviderImpl::LoadStoryRuntimeCall", std::move(result_call)),
        story_provider_impl_(story_provider_impl),
        session_storage_(session_storage),
        story_id_(std::move(story_id)),
        session_inspect_node_(root_node) {}

 private:
  void Run() override {
    FlowToken flow{this, &story_runtime_container_};

    // Use the existing controller, if possible.
    // This won't race against itself because it's managed by an operation
    // queue.
    auto i = story_provider_impl_->story_runtime_containers_.find(story_id_);
    if (i != story_provider_impl_->story_runtime_containers_.end()) {
      story_runtime_container_ = &i->second;
      return;
    }

    auto story_data = session_storage_->GetStoryData(story_id_);
    if (!story_data) {
      return;
      // Operation finishes since |flow| goes out of scope.
    }
    Cont(std::move(story_data), flow);
  }

  void Cont(fuchsia::modular::internal::StoryDataPtr story_data, const FlowToken& flow) {
    auto story_storage = session_storage_->GetStoryStorage(story_id_);
    struct StoryRuntimeContainer container {
      .executor = std::make_unique<async::Executor>(async_get_default_dispatcher()),
      .storage = std::move(story_storage), .current_data = std::move(story_data),
    };

    container.InitializeInspect(story_id_, session_inspect_node_);

    container.controller_impl = std::make_unique<StoryControllerImpl>(
        story_id_, session_storage_, container.storage.get(), story_provider_impl_,
        container.story_node.get(), story_provider_impl_->present_mods_as_stories_);
    auto it =
        story_provider_impl_->story_runtime_containers_.emplace(story_id_, std::move(container));
    story_runtime_container_ = &it.first->second;
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned
  SessionStorage* const session_storage_;         // not owned
  const std::string story_id_;

  inspect::Node* session_inspect_node_;

  // Return value.
  StoryRuntimeContainer* story_runtime_container_ = nullptr;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;
};

class StoryProviderImpl::StopAllStoriesCall : public Operation<> {
 public:
  StopAllStoriesCall(StoryProviderImpl* const story_provider_impl, ResultCall result_call)
      : Operation("StoryProviderImpl::StopAllStoriesCall", std::move(result_call)),
        story_provider_impl_(story_provider_impl) {}

 private:
  void Run() override {
    FlowToken flow{this};

    for (auto& it : story_provider_impl_->story_runtime_containers_) {
      // Each callback has a copy of |flow| which only goes out-of-scope
      // once the story corresponding to |it| stops.
      //
      // TODO(thatguy): If the StoryControllerImpl is deleted before it can
      // complete StopWithoutNotifying(), we will never be called back and the
      // OperationQueue on which we're running will block.  Moving over to
      // fpromise::promise will allow us to observe cancellation.
      operations_.Add(std::make_unique<StopStoryCall>(
          it.first, true /* skip_notifying_sessionshell */,
          &story_provider_impl_->story_runtime_containers_, [flow] {}));
    }
  }

  OperationCollection operations_;

  StoryProviderImpl* const story_provider_impl_;  // not owned
};

class StoryProviderImpl::StopStoryShellCall : public Operation<> {
 public:
  StopStoryShellCall(StoryProviderImpl* const story_provider_impl, ResultCall result_call)
      : Operation("StoryProviderImpl::StopStoryShellCall", std::move(result_call)),
        story_provider_impl_(story_provider_impl) {}

 private:
  void Run() override {
    FlowToken flow{this};
    if (story_provider_impl_->preloaded_story_shell_app_) {
      // Calling Teardown() below will branch |flow| into normal and timeout
      // paths. |flow| must go out of scope when either of the paths
      // finishes.
      FlowTokenHolder branch{flow};
      story_provider_impl_->preloaded_story_shell_app_->Teardown(
          kBasicTimeout, [branch] { std::unique_ptr<FlowToken> flow = branch.Continue(); });
    }
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned
};

StoryProviderImpl::StoryProviderImpl(
    Environment* const session_environment, SessionStorage* const session_storage,
    std::optional<fuchsia::modular::session::AppConfig> story_shell_config,
    fuchsia::modular::StoryShellFactoryPtr story_shell_factory,
    PresentationProtocolPtr presentation_protocol, bool present_mods_as_stories, bool use_flatland,
    ComponentContextInfo component_context_info, AgentServicesFactory* const agent_services_factory,
    inspect::Node* root_node)
    : session_environment_(session_environment),
      session_storage_(session_storage),
      story_shell_config_(std::move(story_shell_config)),
      story_shell_factory_(std::move(story_shell_factory)),
      presentation_protocol_(std::move(presentation_protocol)),
      present_mods_as_stories_(present_mods_as_stories),
      use_flatland_(use_flatland),
      component_context_info_(std::move(component_context_info)),
      agent_services_factory_(agent_services_factory),
      session_inspect_node_(root_node),
      weak_factory_(this) {
  // |presentation_protocol_| must be set to one of the supported protocols.
  FX_DCHECK(!std::holds_alternative<std::monostate>(presentation_protocol_));

  if (auto graphical_presenter =
          std::get_if<fuchsia::element::GraphicalPresenterPtr>(&presentation_protocol_)) {
    graphical_presenter->set_error_handler([](zx_status_t status) {
      FX_PLOGS(ERROR, status)
          << "GraphicalPresenter service channel (from session shell component) "
             "unexpectedly closed.";
    });
  } else if (auto session_shell =
                 std::get_if<fuchsia::modular::SessionShellPtr>(&presentation_protocol_)) {
    session_shell->set_error_handler([](zx_status_t status) {
      FX_PLOGS(ERROR, status) << "SessionShell service channel (from session shell component) "
                                 "unexpectedly closed.";
    });
  } else {
    FX_LOGS(FATAL) << "Unhandled PresentationProtocolPtr alternative: "
                   << presentation_protocol_.index();
    FX_NOTREACHED();
  }

  session_storage_->SubscribeStoryDeleted(
      [weak_this = weak_factory_.GetWeakPtr()](const std::string& story_id) {
        if (!weak_this) {
          return WatchInterest::kStop;
        }
        weak_this->OnStoryStorageDeleted(story_id);
        return WatchInterest::kContinue;
      });
  session_storage_->SubscribeStoryUpdated(
      [weak_this = weak_factory_.GetWeakPtr()](
          std::string story_id, const fuchsia::modular::internal::StoryData& story_data) {
        if (!weak_this) {
          return WatchInterest::kStop;
        }
        weak_this->OnStoryStorageUpdated(std::move(story_id), story_data);
        return WatchInterest::kContinue;
      });

  // Process any stories that were created before StoryProvider was constructed.
  for (const auto& story_data : session_storage_->GetAllStoryData()) {
    OnStoryStorageUpdated(story_data.story_name(), story_data);
  }
}

StoryProviderImpl::~StoryProviderImpl() = default;

void StoryProviderImpl::Connect(fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void StoryProviderImpl::StopAllStories(fit::function<void()> callback) {
  operation_queue_.Add(std::make_unique<StopAllStoriesCall>(this, std::move(callback)));
}

void StoryProviderImpl::Teardown(fit::function<void()> callback) {
  // Closing all binding to this instance ensures that no new messages come
  // in, though previous messages need to be processed. The stopping of
  // stories is done on |operation_queue_| since that must strictly happen
  // after all pending messgages have been processed.
  bindings_.CloseAll();
  if (auto graphical_presenter =
          std::get_if<fuchsia::element::GraphicalPresenterPtr>(&presentation_protocol_)) {
    graphical_presenter->set_error_handler(nullptr);
  } else if (auto session_shell =
                 std::get_if<fuchsia::modular::SessionShellPtr>(&presentation_protocol_)) {
    session_shell->set_error_handler(nullptr);
  } else {
    FX_LOGS(FATAL) << "Unhandled PresentationProtocolPtr alternative: "
                   << presentation_protocol_.index();
    FX_NOTREACHED();
  }
  operation_queue_.Add(std::make_unique<StopAllStoriesCall>(this, [] {}));
  operation_queue_.Add(std::make_unique<StopStoryShellCall>(this, std::move(callback)));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::Watch(
    fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher) {
  auto watcher_ptr = watcher.Bind();
  for (const auto& item : story_runtime_containers_) {
    const auto& container = item.second;
    FX_CHECK(container.current_data->has_story_info());
    watcher_ptr->OnChange2(CloneStruct(container.current_data->story_info()),
                           container.controller_impl->runtime_state(),
                           fuchsia::modular::StoryVisibilityState::DEFAULT);
  }
  watchers_.AddInterfacePtr(std::move(watcher_ptr));
}

StoryControllerImpl* StoryProviderImpl::GetStoryControllerImpl(const std::string& story_id) {
  auto it = story_runtime_containers_.find(story_id);
  if (it == story_runtime_containers_.end()) {
    return nullptr;
  }
  auto& container = it->second;
  return container.controller_impl.get();
}

std::unique_ptr<AsyncHolderBase> StoryProviderImpl::StartStoryShell(
    std::string story_id,
    fidl::InterfaceRequest<fuchsia::modular::StoryShell> story_shell_request) {
  // When we're supplied a StoryShellFactory, use it to get StoryShells instead
  // of launching the story shell as a separate component. In this case, there
  // is also nothing to preload, so ignore |preloaded_story_shell_app_|.
  if (story_shell_factory_) {
    story_shell_factory_->AttachStory(story_id, std::move(story_shell_request));

    auto on_teardown = [this, story_id = std::move(story_id)](fit::function<void()> done) {
      story_shell_factory_->DetachStory(story_id, std::move(done));
    };

    return std::make_unique<ClosureAsyncHolder>(/*name=*/story_id, std::move(on_teardown));
  }

  MaybeLoadStoryShell();
  AttachOrPresentStoryShellView(story_id);

  preloaded_story_shell_app_->services().Connect(std::move(story_shell_request));

  auto story_shell_holder = std::move(preloaded_story_shell_app_);

  return story_shell_holder;
}

void StoryProviderImpl::MaybeLoadStoryShell() {
  if (preloaded_story_shell_app_) {
    return;
  }

  FX_DCHECK(story_shell_config_.has_value()) << "Story shell must be configured.";

  auto service_list = std::make_unique<fuchsia::sys::ServiceList>();
  for (const auto& service_name : component_context_info_.agent_runner->GetAgentServices()) {
    service_list->names.push_back(service_name);
  }
  component_context_info_.agent_runner->PublishAgentServices(story_shell_config_->url(),
                                                             &story_shell_services_);

  fuchsia::sys::ServiceProviderPtr service_provider;
  story_shell_services_.AddBinding(service_provider.NewRequest());
  service_list->provider = std::move(service_provider);

  preloaded_story_shell_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      session_environment_->GetLauncher(), CloneStruct(*story_shell_config_),
      std::move(service_list));
}

void StoryProviderImpl::AttachOrPresentStoryShellView(std::string story_id) {
  FX_DCHECK(preloaded_story_shell_app_);

  AttachOrPresentViewParams present_view_params;
  present_view_params.story_id = std::move(story_id);

  // Create the flatland or gfx view.
  if (use_flatland_) {
    auto [view_creation_token, viewport_creation_token] = scenic::ViewCreationTokenPair::New();
    present_view_params.viewport_creation_token =
        std::make_optional(std::move(viewport_creation_token));
    fuchsia::ui::app::ViewProviderPtr view_provider;
    preloaded_story_shell_app_->services().Connect(view_provider.NewRequest());
    fuchsia::ui::app::CreateView2Args args;
    args.set_view_creation_token(std::move(view_creation_token));
    view_provider->CreateView2(std::move(args));
  } else {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    scenic::ViewRefPair view_ref_pair = scenic::ViewRefPair::New();

    present_view_params.view_holder_token = std::move(view_holder_token),
    present_view_params.view_ref = fidl::Clone(view_ref_pair.view_ref);

    fuchsia::ui::app::ViewProviderPtr view_provider;
    preloaded_story_shell_app_->services().Connect(view_provider.NewRequest());
    view_provider->CreateViewWithViewRef(std::move(view_token.value),
                                         std::move(view_ref_pair.control_ref),
                                         std::move(view_ref_pair.view_ref));
  }

  AttachOrPresentView(std::move(present_view_params));
}

fuchsia::modular::StoryInfo2Ptr StoryProviderImpl::GetCachedStoryInfo(const std::string& story_id) {
  auto it = story_runtime_containers_.find(story_id);
  if (it == story_runtime_containers_.end()) {
    return nullptr;
  }
  FX_CHECK(it->second.current_data->has_story_info());
  return CloneOptional(it->second.current_data->story_info());
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetStoryInfo(std::string story_id, GetStoryInfoCallback callback) {
  operation_queue_.Add(std::make_unique<SyncCall>([this, story_id, callback = std::move(callback)] {
    auto story_data = session_storage_->GetStoryData(story_id);
    if (!story_data || !story_data->has_story_info()) {
      callback(nullptr);
      return;
    }
    callback(std::make_unique<fuchsia::modular::StoryInfo>(
        StoryInfo2ToStoryInfo(story_data->story_info())));
  }));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetStoryInfo2(std::string story_id, GetStoryInfo2Callback callback) {
  operation_queue_.Add(std::make_unique<SyncCall>([this, story_id, callback = std::move(callback)] {
    auto story_data = session_storage_->GetStoryData(story_id);
    if (!story_data || !story_data->has_story_info()) {
      callback(fuchsia::modular::StoryInfo2{});
      return;
    }
    callback(std::move(*story_data->mutable_story_info()));
  }));
}

void StoryProviderImpl::AttachOrPresentView(AttachOrPresentViewParams params) {
  if (is_graphical_presenter_presentation()) {
    PresentView(std::move(params));
  } else if (is_session_shell_presentation()) {
    AttachView(std::move(params));
  } else {
    FX_LOGS(FATAL) << "Unhandled PresentationProtocolPtr alternative: "
                   << presentation_protocol_.index();
    FX_NOTREACHED();
  }
}

void StoryProviderImpl::DetachOrDismissView(const std::string& story_id,
                                            fit::function<void()> done) {
  if (is_graphical_presenter_presentation()) {
    DismissView(story_id, std::move(done));
  } else if (is_session_shell_presentation()) {
    DetachView(story_id, std::move(done));
  } else {
    FX_LOGS(FATAL) << "Unhandled PresentationProtocolPtr alternative: "
                   << presentation_protocol_.index();
    FX_NOTREACHED();
  }
}

void StoryProviderImpl::AttachView(AttachOrPresentViewParams params) {
  FX_DCHECK(params.view_holder_token.has_value() || params.viewport_creation_token.has_value())
      << "AttachView expects either a ViewHolder or ViewportCreation token";
  FX_DCHECK(is_session_shell_presentation())
      << "AttachView expects a SessionShellPtr PresentationProtocolPtr";
  auto& session_shell = std::get<fuchsia::modular::SessionShellPtr>(presentation_protocol_);
  FX_CHECK(session_shell.get())
      << "The session shell component must keep alive a "
         "fuchsia.modular.SessionShell service for sessionmgr to function.";
  fuchsia::modular::ViewIdentifier view_id;
  view_id.story_id = std::move(params.story_id);
  if (params.view_holder_token.has_value()) {
    session_shell->AttachView2(std::move(view_id), std::move(params.view_holder_token.value()));
  } else {
    session_shell->AttachView3(std::move(view_id),
                               std::move(params.viewport_creation_token.value()));
  }
}

void StoryProviderImpl::DetachView(std::string story_id, fit::function<void()> done) {
  FX_DCHECK(is_session_shell_presentation())
      << "DetachView expects a SessionShellPtr PresentationProtocolPtr";
  auto& session_shell = std::get<fuchsia::modular::SessionShellPtr>(presentation_protocol_);
  FX_CHECK(session_shell.get())
      << "The session shell component must keep alive a "
         "fuchsia.modular.SessionShell service for sessionmgr to function.";
  fuchsia::modular::ViewIdentifier view_id;
  view_id.story_id = std::move(story_id);
  session_shell->DetachView(std::move(view_id), std::move(done));
}

void StoryProviderImpl::PresentView(AttachOrPresentViewParams params) {
  FX_DCHECK(params.view_holder_token.has_value() || params.viewport_creation_token.has_value());
  FX_DCHECK(is_graphical_presenter_presentation())
      << "PresentView expects a GraphicalPresenter PresentationProtocolPtr";
  auto& graphical_presenter =
      std::get<fuchsia::element::GraphicalPresenterPtr>(presentation_protocol_);
  FX_CHECK(graphical_presenter.get())
      << "The session shell component must keep alive a fuchsia.element.GraphicalPresenter service "
         "for sessionmgr to function.";

  fuchsia::element::ViewSpec view_spec;
  if (params.viewport_creation_token.has_value()) {
    view_spec.set_viewport_creation_token(std::move(params.viewport_creation_token.value()));
  } else {
    view_spec.set_view_holder_token(std::move(params.view_holder_token.value()));
    if (params.view_ref.has_value()) {
      view_spec.set_view_ref(std::move(params.view_ref.value()));
    }
  }

  fuchsia::modular::internal::StoryDataPtr story_data =
      session_storage_->GetStoryData(params.story_id);
  if (!story_data) {
    FX_LOGS(WARNING) << "Not presenting view, story does not exist: " << params.story_id;
    return;
  }

  if (story_data->story_info().has_annotations()) {
    view_spec.set_annotations(
        annotations::ToElementAnnotations(story_data->story_info().annotations()));
  }

  fuchsia::element::ViewControllerPtr view_controller;
  view_controller.set_error_handler([weak_this = weak_factory_.GetWeakPtr(),
                                     story_id = params.story_id](zx_status_t status) {
    if (!weak_this) {
      return;
    }

    auto finish_dismiss = [weak_this, story_id]() {
      for (auto& callback : weak_this->dismiss_callbacks_[story_id]) {
        callback();
      }

      // Remove view controllers from the map
      weak_this->view_controllers_.erase(story_id);
      weak_this->dismiss_callbacks_.erase(story_id);
      weak_this->annotation_controllers_.erase(story_id);
    };

    // Check if the story is already deleted, stopped, or stopping.
    // If it is, DismissView was previously called and the client closed ViewController
    // in response, and there's no need to stop the story again.
    auto it = weak_this->story_runtime_containers_.find(story_id);
    if (it == weak_this->story_runtime_containers_.end() ||
        it->second.controller_impl->runtime_state() == fuchsia::modular::StoryState::STOPPED ||
        it->second.controller_impl->runtime_state() == fuchsia::modular::StoryState::STOPPING) {
      finish_dismiss();
    } else {
      // Otherwise, the client closed the ViewController while the story was running,
      // so treat is as a request to stop the story.
      FX_PLOGS(WARNING, status) << "ViewController connection closed, stopping story: " << story_id;

      weak_this->operation_queue_.Add(std::make_unique<StopStoryCall>(
          story_id, /*skip_notifying_sessionshell=*/false, &weak_this->story_runtime_containers_,
          [weak_this, story_id, finish_dismiss = std::move(finish_dismiss)] {
            // Delete the story
            weak_this->session_storage_->DeleteStory(story_id);
            finish_dismiss();
          }));
    }
  });

  fuchsia::element::AnnotationControllerPtr annotation_controller;
  annotation_controller.set_error_handler(
      [weak_this = weak_factory_.GetWeakPtr(), story_id = params.story_id](zx_status_t status) {
        if (!weak_this) {
          return;
        }

        // Remove annotation controller from the map
        weak_this->annotation_controllers_.erase(story_id);
      });
  auto annotation_controller_impl =
      std::make_unique<AnnotationControllerImpl>(params.story_id, session_storage_);
  annotation_controller_impl->Connect(annotation_controller.NewRequest());

  graphical_presenter->PresentView(
      std::move(view_spec), std::move(annotation_controller), view_controller.NewRequest(),
      [weak_this = weak_factory_.GetWeakPtr(), story_id = std::move(params.story_id),
       view_controller = std::move(view_controller),
       annotation_controller_impl = std::move(annotation_controller_impl)](
          const fuchsia::element::GraphicalPresenter_PresentView_Result& result) mutable {
        if (!weak_this) {
          return;
        }

        if (result.is_err()) {
          if (result.err() == fuchsia::element::PresentViewError::INVALID_ARGS) {
            FX_LOGS(ERROR) << "Error presenting view: PresentViewError "
                           << static_cast<uint32_t>(result.err()) << " (INVALID_ARGS). "
                           << "This is a bug!";
          } else {
            FX_LOGS(WARNING) << "Error presenting view: PresentViewError: "
                             << static_cast<uint32_t>(result.err());
          }
          return;
        }

        weak_this->view_controllers_[story_id].push_back(std::move(view_controller));
        weak_this->annotation_controllers_[story_id] = std::move(annotation_controller_impl);
      });
}

void StoryProviderImpl::DismissView(const std::string& story_id, fit::function<void()> done) {
  FX_DCHECK(is_graphical_presenter_presentation())
      << "DismissView expects a GraphicalPresenter PresentationProtocolPtr";
  auto& graphical_presenter =
      std::get<fuchsia::element::GraphicalPresenterPtr>(presentation_protocol_);
  FX_CHECK(graphical_presenter.get())
      << "The session shell component keep alive a fuchsia.element.GraphicalPresenter service for "
         "sessionmgr to function.";

  auto controllers_it = view_controllers_.find(story_id);
  if (controllers_it == view_controllers_.end()) {
    FX_LOGS(WARNING) << "Not dismissing view, story ViewController does not exist: " << story_id;
    dismiss_callbacks_.erase(story_id);
    annotation_controllers_.erase(story_id);
    done();
    return;
  }

  for (auto it = view_controllers_[story_id].begin(); it != view_controllers_[story_id].end();) {
    // Notify the ViewController to Dismiss the view, if it's connected, or erase the
    // ViewController if it isn't.
    if (auto& view_controller = *it) {
      view_controller->Dismiss();
      ++it;
    } else {
      it = view_controllers_[story_id].erase(it);
    }
  }

  dismiss_callbacks_[story_id].push_back(std::move(done));

  // If all ViewControllers have been deleted because they are disconnected, clean up.
  if (controllers_it->second.empty()) {
    for (auto& callback : dismiss_callbacks_[story_id]) {
      callback();
    }

    view_controllers_.erase(story_id);
    dismiss_callbacks_.erase(story_id);
    annotation_controllers_.erase(story_id);
  }
}

void StoryProviderImpl::NotifyStoryStateChange(const std::string& story_id) {
  auto it = story_runtime_containers_.find(story_id);
  if (it == story_runtime_containers_.end()) {
    // If this call arrives while DeleteStory() is in
    // progress, the story controller might already be gone
    // from here.
    return;
  }
  NotifyStoryWatchers(it->second.current_data.get(), it->second.controller_impl->runtime_state());
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetController(
    std::string story_id, fidl::InterfaceRequest<fuchsia::modular::StoryController> request) {
  operation_queue_.Add(std::make_unique<LoadStoryRuntimeCall>(
      this, session_storage_, story_id, session_inspect_node_,
      [request = std::move(request)](StoryRuntimeContainer* story_controller_container) mutable {
        if (story_controller_container) {
          story_controller_container->controller_impl->Connect(std::move(request));
        }
      }));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetStories(
    fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher,
    GetStoriesCallback callback) {
  operation_queue_.Add(std::make_unique<SyncCall>(
      [this, watcher = std::move(watcher), callback = std::move(callback)]() mutable {
        auto all_story_data = session_storage_->GetAllStoryData();
        std::vector<fuchsia::modular::StoryInfo> result;

        for (auto& story_data : all_story_data) {
          if (!story_data.has_story_info()) {
            continue;
          }
          result.push_back(StoryInfo2ToStoryInfo(story_data.story_info()));
        }

        if (watcher) {
          watchers_.AddInterfacePtr(watcher.Bind());
        }
        callback(std::move(result));
      }));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetStories2(
    fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher,
    GetStories2Callback callback) {
  operation_queue_.Add(std::make_unique<SyncCall>(
      [this, watcher = std::move(watcher), callback = std::move(callback)]() mutable {
        auto all_story_data = session_storage_->GetAllStoryData();
        std::vector<fuchsia::modular::StoryInfo2> result;

        for (auto& story_data : all_story_data) {
          if (!story_data.has_story_info()) {
            continue;
          }
          result.push_back(std::move(*story_data.mutable_story_info()));
        }

        if (watcher) {
          watchers_.AddInterfacePtr(watcher.Bind());
        }
        callback(std::move(result));
      }));
}

void StoryProviderImpl::OnStoryStorageUpdated(
    std::string story_id, const fuchsia::modular::internal::StoryData& story_data) {
  // If we have a StoryRuntimeContainer for this story id, update our cached
  // StoryData and get runtime state available from it.
  //
  // Otherwise, use defaults for an unloaded story and send a request for the
  // story to start running (stories should start running by default).
  fuchsia::modular::StoryState runtime_state = fuchsia::modular::StoryState::STOPPED;
  auto it = story_runtime_containers_.find(story_data.story_info().id());
  if (it != story_runtime_containers_.end()) {
    auto& container = it->second;
    runtime_state = container.controller_impl->runtime_state();
    container.current_data = CloneOptional(story_data);
    container.ResetInspect();
  } else {
    fuchsia::modular::StoryControllerPtr story_controller;
    GetController(std::move(story_id), story_controller.NewRequest());
    story_controller->RequestStart();
  }
  NotifyStoryWatchers(&story_data, runtime_state);
}

void StoryProviderImpl::OnStoryStorageDeleted(const std::string& story_id) {
  operation_queue_.Add(
      std::make_unique<StopStoryCall>(story_id, false /* skip_notifying_sessionshell */,
                                      &story_runtime_containers_, [this, story_id] {
                                        for (const auto& i : watchers_.ptrs()) {
                                          (*i)->OnDelete(story_id);
                                        }
                                      }));
}

void StoryProviderImpl::NotifyStoryWatchers(const fuchsia::modular::internal::StoryData* story_data,
                                            const fuchsia::modular::StoryState story_state) {
  if (!story_data) {
    return;
  }
  for (const auto& i : watchers_.ptrs()) {
    if (!story_data->has_story_info()) {
      continue;
    }
    (*i)->OnChange2(CloneStruct(story_data->story_info()), story_state,
                    fuchsia::modular::StoryVisibilityState::DEFAULT);
  }
}

fuchsia::modular::StoryInfo StoryProviderImpl::StoryInfo2ToStoryInfo(
    const fuchsia::modular::StoryInfo2& story_info_2) {
  fuchsia::modular::StoryInfo story_info;

  story_info.id = story_info_2.id();
  story_info.last_focus_time = story_info_2.last_focus_time();

  return story_info;
}

void StoryProviderImpl::StoryRuntimeContainer::InitializeInspect(
    std::string story_id, inspect::Node* session_inspect_node) {
  story_node = std::make_unique<inspect::Node>(session_inspect_node->CreateChild(story_id));
  ResetInspect();
}

void StoryProviderImpl::StoryRuntimeContainer::ResetInspect() {
  if (current_data->story_info().has_annotations()) {
    for (const fuchsia::modular::Annotation& annotation :
         current_data->story_info().annotations()) {
      std::string value_str = modular::annotations::ToInspect(*annotation.value);
      std::string key_with_prefix = "annotation: " + annotation.key;
      if (annotation_inspect_properties.find(key_with_prefix) !=
          annotation_inspect_properties.end()) {
        annotation_inspect_properties[key_with_prefix].Set(value_str);
      } else {
        annotation_inspect_properties.insert(std::pair<const std::string, inspect::StringProperty>(
            annotation.key, story_node->CreateString(key_with_prefix, value_str)));
      }
    }
  }
}

}  // namespace modular
