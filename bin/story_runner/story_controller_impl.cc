// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/story_controller_impl.h"

#include <memory>

#include <fuchsia/cpp/views_v1.h>
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/component.h>
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/join_strings.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/module/fidl/module_context.fidl.h"
#include "lib/module/fidl/module_data.fidl.h"
#include "lib/story/fidl/create_link.fidl.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/story/fidl/story_marker.fidl.h"
#include "peridot/bin/device_runner/cobalt/cobalt.h"
#include "peridot/bin/story_runner/chain_impl.h"
#include "peridot/bin/story_runner/link_impl.h"
#include "peridot/bin/story_runner/module_context_impl.h"
#include "peridot/bin/story_runner/module_controller_impl.h"
#include "peridot/bin/story_runner/story_provider_impl.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/storage.h"

namespace modular {

constexpr char kStoryScopeLabelPrefix[] = "story-";

namespace {

f1dl::StringPtr PathString(const f1dl::VectorPtr<f1dl::StringPtr>& module_path) {
  // f1dl::StringPtr no longer supports size(), begin() or end(). JoinStrings()
  // only supports element types with those methods.
  std::vector<std::string> path_vec(module_path->begin(), module_path->end());
  return fxl::JoinStrings(path_vec, ":");
}

f1dl::VectorPtr<f1dl::StringPtr> ParentModulePath(
    const f1dl::VectorPtr<f1dl::StringPtr>& module_path) {
  auto ret = module_path.Clone();
  if (ret->size() > 0) {
    // Root is its own parent.
    ret.resize(ret->size() - 1);
  }
  return ret;
}

void XdrLinkPath(XdrContext* const xdr, LinkPath* const data) {
  xdr->Field("module_path", &data->module_path);
  xdr->Field("link_name", &data->link_name);
}

void XdrChainKeyToLinkData(XdrContext* const xdr,
                           ChainKeyToLinkData* const data) {
  xdr->Field("key", &data->key);
  xdr->Field("link_path", &data->link_path, XdrLinkPath);
}

void XdrChainData(XdrContext* const xdr, ChainData* const data) {
  xdr->Field("key_to_link_map", &data->key_to_link_map, XdrChainKeyToLinkData);
}

void XdrSurfaceRelation(XdrContext* const xdr, SurfaceRelation* const data) {
  xdr->Field("arrangement", &data->arrangement);
  xdr->Field("dependency", &data->dependency);
  xdr->Field("emphasis", &data->emphasis);
}

void XdrNoun(XdrContext* const xdr, Noun* const data) {
  static constexpr char kTag[] = "tag";
  static constexpr char kEntityReference[] = "entity_reference";
  static constexpr char kJson[] = "json";
  static constexpr char kEntityType[] = "entity_type";
  static constexpr char kLinkName[] = "link_name";
  static constexpr char kLinkPath[] = "link_path";

  switch (xdr->op()) {
    case XdrOp::FROM_JSON: {
      std::string tag;
      xdr->Field(kTag, &tag);

      if (tag == kEntityReference) {
        std::string value;
        xdr->Field(kEntityReference, &value);
        data->set_entity_reference(std::move(value));
      } else if (tag == kJson) {
        std::string value;
        xdr->Field(kJson, &value);
        data->set_json(std::move(value));
      } else if (tag == kEntityType) {
        ::f1dl::VectorPtr<::f1dl::String> value;
        xdr->Field(kEntityType, &value);
        data->set_entity_type(std::move(value));
      } else if (tag == kLinkName) {
        std::string value;
        xdr->Field(kLinkName, &value);
        data->set_link_name(std::move(value));
      } else if (tag == kLinkPath) {
        LinkPathPtr value;
        xdr->Field(kLinkPath, &value, XdrLinkPath);
        data->set_link_path(std::move(value));
      } else {
        FXL_LOG(ERROR) << "XdrNoun FROM_JSON unknown tag: " << tag;
      }
      break;
    }

    case XdrOp::TO_JSON: {
      std::string tag;

      // The unusual call to operator->() in the cases below is because
      // operator-> for all of FIDL's pointer types to {strings, arrays,
      // structs} returns a _non-const_ reference to the inner pointer,
      // which is required by the xdr->Field() method. Calling get() returns
      // a const pointer for arrays and strings. get() does return a non-const
      // pointer for FIDL structs, but given that operator->() is required for
      // some FIDL types, we might as well be consistent and use operator->()
      // for all types.

      switch (data->which()) {
        case Noun::Tag::ENTITY_REFERENCE:
          tag = kEntityReference;
          xdr->Field(kEntityReference,
                     data->get_entity_reference().operator->());
          break;
        case Noun::Tag::JSON:
          tag = kJson;
          xdr->Field(kJson, data->get_json().operator->());
          break;
        case Noun::Tag::ENTITY_TYPE:
          tag = kEntityType;
          xdr->Field(kEntityType, data->get_entity_type().operator->());
          break;
        case Noun::Tag::LINK_NAME:
          tag = kLinkName;
          xdr->Field(kLinkName, data->get_link_name().operator->());
          break;
        case Noun::Tag::LINK_PATH: {
          tag = kLinkPath;
          xdr->Field(kLinkPath, data->get_link_path().operator->(),
                     XdrLinkPath);
          break;
        }
        case Noun::Tag::__UNKNOWN__:
          FXL_LOG(ERROR) << "XdrNoun TO_JSON unknown tag: "
                         << static_cast<int>(data->which());
          break;
      }

      xdr->Field(kTag, &tag);
      break;
    }
  }
}

void XdrNounEntry(XdrContext* const xdr, NounEntry* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("noun", &data->noun, XdrNoun);
}

void XdrDaisy(XdrContext* const xdr, Daisy* const data) {
  xdr->Field("verb", &data->verb);
  xdr->Field("url", &data->url);
  xdr->Field("nouns", &data->nouns, XdrNounEntry);
}

void XdrModuleData(XdrContext* const xdr, ModuleData* const data) {
  xdr->Field("url", &data->module_url);
  xdr->Field("module_path", &data->module_path);
  // TODO(mesch): Rename the XDR field eventually.
  xdr->Field("default_link_path", &data->link_path, XdrLinkPath);
  xdr->Field("module_source", &data->module_source);
  xdr->Field("surface_relation", &data->surface_relation, XdrSurfaceRelation);
  xdr->Field("module_stopped", &data->module_stopped);
  xdr->Field("daisy", &data->daisy, XdrDaisy);

  xdr->ReadErrorHandler([data] {
       data->chain_data = ChainData::New();
       data->chain_data->key_to_link_map.resize(0);
     })
      ->Field("chain_data", &data->chain_data, XdrChainData);
}

void XdrPerDeviceStoryInfo(XdrContext* const xdr,
                           PerDeviceStoryInfo* const info) {
  xdr->Field("device", &info->device_id);
  xdr->Field("id", &info->story_id);
  xdr->Field("time", &info->timestamp);
  xdr->Field("state", &info->state);
}

}  // namespace

class StoryControllerImpl::StoryMarkerImpl : StoryMarker {
 public:
  StoryMarkerImpl() = default;
  ~StoryMarkerImpl() override = default;

  void Connect(f1dl::InterfaceRequest<StoryMarker> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  f1dl::BindingSet<StoryMarker> bindings_;
  FXL_DISALLOW_COPY_AND_ASSIGN(StoryMarkerImpl);
};

class StoryControllerImpl::BlockingModuleDataWriteCall : Operation<> {
 public:
  BlockingModuleDataWriteCall(OperationContainer* const container,
                              StoryControllerImpl* const story_controller_impl,
                              std::string key,
                              ModuleDataPtr module_data,
                              ResultCall result_call)
      : Operation("StoryControllerImpl::BlockingModuleDataWriteCall",
                  container,
                  std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        key_(std::move(key)),
        module_data_(std::move(module_data)) {
    story_controller_impl_->blocked_operations_.push_back(
        std::make_pair(module_data_.Clone(), this));
    Ready();
  }

  void Continue() {
    fn_called_ = true;
    if (fn_) {
      fn_();
    }
  }

 private:
  void Run() override {
    FlowToken flow{this};

    new WriteDataCall<ModuleData>(
        &operation_queue_, story_controller_impl_->page(), key_, XdrModuleData,
        std::move(module_data_), [this, flow] {
          FlowTokenHolder hold{flow};
          fn_ = [hold] {
            std::unique_ptr<FlowToken> flow = hold.Continue();
            FXL_CHECK(flow) << "Called BlockingModuleDataWriteCall::Continue() "
                            << "twice. Please file a bug.";
          };

          if (fn_called_) {
            fn_();
          }
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const std::string key_;
  ModuleDataPtr module_data_;

  std::function<void()> fn_;
  bool fn_called_{};

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BlockingModuleDataWriteCall);
};

class StoryControllerImpl::LaunchModuleCall : Operation<> {
 public:
  LaunchModuleCall(
      OperationContainer* const container,
      StoryControllerImpl* const story_controller_impl,
      ModuleDataPtr module_data,
      f1dl::InterfaceRequest<component::ServiceProvider> incoming_services,
      f1dl::InterfaceRequest<ModuleController> module_controller_request,
      f1dl::InterfaceHandle<EmbedModuleWatcher> embed_module_watcher,
      fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
      ResultCall result_call)
      : Operation("StoryControllerImpl::GetLedgerNotificationCall", container,
                  std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)),
        incoming_services_(std::move(incoming_services)),
        module_controller_request_(std::move(module_controller_request)),
        embed_module_watcher_(std::move(embed_module_watcher)),
        view_owner_request_(std::move(view_owner_request)),
        start_time_(zx_clock_get(ZX_CLOCK_UTC)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    Connection* const i =
        story_controller_impl_->FindConnection(module_data_->module_path);

    // We launch the new module if it doesn't run yet.
    if (!i) {
      Launch(flow);
      return;
    }

    // If the new module is already running, but with a different URL or on a
    // different link, or if a service exchange is requested, or if transitive
    // embedding is requested, we tear it down then launch a new module.
    //
    // TODO(mesch): If only the link is different, we should just hook the
    // existing module instance on a new link and notify it about the changed
    // link value.
    if (i->module_data->module_url != module_data_->module_url ||
        !i->module_data->link_path->Equals(*module_data_->link_path) ||
        !i->module_data->chain_data->Equals(*module_data_->chain_data) ||
        embed_module_watcher_.is_valid() || incoming_services_.is_valid()) {
      i->module_controller_impl->Teardown([this, flow] {
        // NOTE(mesch): i is invalid at this point.
        Launch(flow);
      });
      return;
    }

    // If the module is already running on the same URL and link, we just
    // connect the module controller request, if there is one. Modules started
    // with StoryController.AddModule() don't have a module controller request.
    if (module_controller_request_.is_valid()) {
      i->module_controller_impl->Connect(std::move(module_controller_request_));
    }
  }

  void Launch(FlowToken /*flow*/) {
    FXL_LOG(INFO) << "StoryControllerImpl::LaunchModule() "
                  << module_data_->module_url << " "
                  << PathString(module_data_->module_path);
    auto module_config = AppConfig::New();
    module_config->url = module_data_->module_url;

    views_v1::ViewProviderPtr view_provider;
    f1dl::InterfaceRequest<views_v1::ViewProvider> view_provider_request =
        view_provider.NewRequest();
    view_provider->CreateView(std::move(view_owner_request_), nullptr);

    component::ServiceProviderPtr provider;
    auto provider_request = provider.NewRequest();
    auto module_context =
        component::ConnectToService<ModuleContext>(provider.get());
    auto service_list = component::ServiceList::New();
    service_list->names.push_back(ModuleContext::Name_);
    service_list->provider = std::move(provider);

    Connection connection;
    connection.module_data = module_data_.Clone();

    if (embed_module_watcher_.is_valid()) {
      connection.embed_module_watcher.Bind(std::move(embed_module_watcher_));
    }

    // Ensure that the Module's Chain is available before we launch it.
    // TODO(thatguy): Set up the ChainImpl based on information in ModuleData.
    auto i = std::find_if(
        story_controller_impl_->chains_.begin(),
        story_controller_impl_->chains_.end(),
        [this](const std::unique_ptr<ChainImpl>& ptr) {
          return ptr->chain_path().Equals(module_data_->module_path);
        });
    if (i == story_controller_impl_->chains_.end()) {
      story_controller_impl_->chains_.emplace_back(new ChainImpl(
          module_data_->module_path.Clone(), module_data_->chain_data.Clone()));
    }

    // ModuleControllerImpl's constructor launches the child application.
    connection.module_controller_impl = std::make_unique<ModuleControllerImpl>(
        story_controller_impl_,
        story_controller_impl_->story_scope_.GetLauncher(),
        std::move(module_config), connection.module_data.get(),
        std::move(service_list), std::move(module_context),
        std::move(view_provider_request), std::move(incoming_services_));

    // Modules started with StoryController.AddModule() don't have a module
    // controller request.
    if (module_controller_request_.is_valid()) {
      connection.module_controller_impl->Connect(
          std::move(module_controller_request_));
    }

    ModuleContextInfo module_context_info = {
        story_controller_impl_->story_provider_impl_->component_context_info(),
        story_controller_impl_, story_controller_impl_->story_provider_impl_
                                    ->user_intelligence_provider(),
        story_controller_impl_->story_provider_impl_->module_resolver()};

    connection.module_context_impl = std::make_unique<ModuleContextImpl>(
        module_context_info, connection.module_data.get(),
        connection.module_controller_impl.get(), std::move(provider_request));

    story_controller_impl_->connections_.emplace_back(std::move(connection));

    story_controller_impl_->watchers_.ForAllPtrs(
        [this](StoryWatcher* const watcher) {
          watcher->OnModuleAdded(module_data_.Clone());
        });

    story_controller_impl_->modules_watchers_.ForAllPtrs(
        [this](StoryModulesWatcher* const watcher) {
          watcher->OnNewModule(module_data_.Clone());
        });
    ReportModuleLaunchTime(module_data_->module_url,
                           zx_clock_get(ZX_CLOCK_UTC) - start_time_);
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  ModuleDataPtr module_data_;
  f1dl::InterfaceRequest<component::ServiceProvider> incoming_services_;
  f1dl::InterfaceRequest<ModuleController> module_controller_request_;
  f1dl::InterfaceHandle<EmbedModuleWatcher> embed_module_watcher_;
  fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request_;
  const zx_time_t start_time_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LaunchModuleCall);
};

class StoryControllerImpl::KillModuleCall : Operation<> {
 public:
  KillModuleCall(OperationContainer* const container,
                 StoryControllerImpl* const story_controller_impl,
                 ModuleDataPtr module_data,
                 const std::function<void()>& done)
      : Operation("StoryControllerImpl::KillModuleCall", container, [] {}),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)),
        done_(done) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};
    // If the module is external, we also notify story shell about it going
    // away. An internal module is stopped by its parent module, and it's up to
    // the parent module to defocus it first. TODO(mesch): Why not always
    // defocus?
    if (story_controller_impl_->story_shell_ &&
        module_data_->module_source == ModuleSource::EXTERNAL) {
      story_controller_impl_->story_shell_->DefocusView(
          PathString(module_data_->module_path), [this, flow] { Cont(flow); });
    } else {
      Cont(flow);
    }
  }

  void Cont(FlowToken flow) {
    // Teardown the module, which discards the module controller. A parent
    // module can call ModuleController.Stop() multiple times before the
    // ModuleController connection gets disconnected by Teardown(). Therefore,
    // this StopModuleCall Operation will cause the calls to be queued.
    // The first Stop() will cause the ModuleController to be closed, and
    // so subsequent Stop() attempts will not find a controller and will return.
    auto* const i =
        story_controller_impl_->FindConnection(module_data_->module_path);

    if (!i) {
      FXL_LOG(INFO) << "No ModuleController for Module"
                    << " " << PathString(module_data_->module_path) << ". "
                    << "Was ModuleContext.Stop() called twice?";
      done_();
      return;
    }

    // done_() must be called BEFORE the Teardown() done callback returns. See
    // comment in StopModuleCall::Kill() before making changes here. Be aware
    // that done_ is NOT the Done() callback of the Operation.
    i->module_controller_impl->Teardown([this, flow] {
      Cont1(flow);
      done_();
    });
  }

  void Cont1(FlowToken /*flow*/) {
    story_controller_impl_->modules_watchers_.ForAllPtrs(
        [this](StoryModulesWatcher* const watcher) {
          watcher->OnStopModule(module_data_.Clone());
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  ModuleDataPtr module_data_;
  std::function<void()> done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(KillModuleCall);
};

class StoryControllerImpl::ConnectLinkCall : Operation<> {
 public:
  // TODO(mesch/thatguy): Notifying watchers on new Link connections is overly
  // complex. Sufficient and simpler would be to have a Story watchers notified
  // of Link state changes for all Links within a Story.
  ConnectLinkCall(OperationContainer* const container,
                  StoryControllerImpl* const story_controller_impl,
                  LinkPathPtr link_path,
                  LinkImpl::ConnectionType connection_type,
                  CreateLinkInfoPtr create_link_info,
                  bool notify_watchers,
                  f1dl::InterfaceRequest<Link> request,
                  ResultCall done)
      : Operation("StoryControllerImpl::ConnectLinkCall",
                  container,
                  std::move(done)),
        story_controller_impl_(story_controller_impl),
        link_path_(std::move(link_path)),
        connection_type_(connection_type),
        create_link_info_(std::move(create_link_info)),
        notify_watchers_(notify_watchers),
        request_(std::move(request)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};
    auto i = std::find_if(story_controller_impl_->links_.begin(),
                          story_controller_impl_->links_.end(),
                          [this](const std::unique_ptr<LinkImpl>& l) {
                            return l->link_path().Equals(link_path_);
                          });
    if (i != story_controller_impl_->links_.end()) {
      (*i)->Connect(std::move(request_), connection_type_);
      return;
    }

    link_impl_.reset(
        new LinkImpl(story_controller_impl_->ledger_client_,
                     story_controller_impl_->story_page_id_.Clone(),
                     link_path_.Clone(), std::move(create_link_info_)));
    LinkImpl* const link_ptr = link_impl_.get();
    if (request_) {
      link_impl_->Connect(std::move(request_), connection_type_);
      // Transfer ownership of |link_impl_| over to |story_controller_impl_|.
      story_controller_impl_->links_.emplace_back(link_impl_.release());

      // This orphaned handler will be called after this operation has been
      // deleted. So we need to take special care when depending on members.
      // Copies of |story_controller_impl_| and |link_ptr| are ok.
      link_ptr->set_orphaned_handler([
        link_ptr, story_controller_impl = story_controller_impl_
      ] { story_controller_impl->DisposeLink(link_ptr); });
    }

    link_ptr->Sync([this, flow] { Cont(flow); });
  }

  void Cont(FlowToken token) {
    if (!notify_watchers_)
      return;

    story_controller_impl_->links_watchers_.ForAllPtrs(
        [this](StoryLinksWatcher* const watcher) {
          watcher->OnNewLink(link_path_.Clone());
        });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const LinkPathPtr link_path_;
  LinkImpl::ConnectionType connection_type_;
  CreateLinkInfoPtr create_link_info_;
  const bool notify_watchers_;
  f1dl::InterfaceRequest<Link> request_;

  std::unique_ptr<LinkImpl> link_impl_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ConnectLinkCall);
};

// Populates a ChainData struct from a CreateChainInfo struct. May create new
// Links for any CreateChainInfo.property_info if
// property_info[i].is_create_link_info().
class StoryControllerImpl::InitializeChainCall : Operation<ChainDataPtr> {
 public:
  InitializeChainCall(OperationContainer* const container,
                      StoryControllerImpl* const story_controller_impl,
                      f1dl::VectorPtr<f1dl::StringPtr> module_path,
                      CreateChainInfoPtr create_chain_info,
                      ResultCall result_call)
      : Operation("InitializeChainCall", container, std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        module_path_(std::move(module_path)),
        create_chain_info_(std::move(create_chain_info)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &result_};

    result_ = ChainData::New();
    result_->key_to_link_map.resize(0);

    if (!create_chain_info_) {
      return;
    }

    // For each property in |create_chain_info_|, either:
    // a) Copy the |link_path| to |result_| directly or
    // b) Create & populate a new Link and add the correct mapping to
    // |result_|.
    for (auto& entry : *create_chain_info_->property_info) {
      const auto& key = entry->key;
      const auto& info = entry->value;

      auto mapping = ChainKeyToLinkData::New();
      mapping->key = key;
      if (info->is_link_path()) {
        mapping->link_path = info->get_link_path().Clone();
      } else {  // info->is_create_link()
        // Create a new Link. ConnectLinkCall will either create a new Link, or
        // connect to an existing one.
        // TODO(thatguy): If the Link already exists (it shouldn't),
        // |create_link_info.initial_data| will be ignored.
        mapping->link_path = LinkPath::New();
        mapping->link_path->module_path = module_path_.Clone();
        mapping->link_path->link_name = key;

        // We create N ConnectLinkCall operations. We rely on the fact that
        // once all refcounted instances of |flow| are destroyed, the
        // InitializeChainCall will automatically finish.
        new ConnectLinkCall(
            &operation_queue_, story_controller_impl_,
            mapping->link_path.Clone(), LinkImpl::ConnectionType::Secondary,
            info->get_create_link().Clone(), false /* notify_watchers */,
            nullptr /* interface request */, [flow] {});
      }

      result_->key_to_link_map.push_back(std::move(mapping));
    }
  }

  StoryControllerImpl* const story_controller_impl_;
  const f1dl::VectorPtr<f1dl::StringPtr> module_path_;
  const CreateChainInfoPtr create_chain_info_;

  OperationQueue operation_queue_;

  ChainDataPtr result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InitializeChainCall);
};

class StoryControllerImpl::StartModuleCall : Operation<> {
 public:
  StartModuleCall(
      OperationContainer* const container,
      StoryControllerImpl* const story_controller_impl,
      const f1dl::VectorPtr<f1dl::StringPtr>& module_path,
      const f1dl::StringPtr& module_url, const f1dl::StringPtr& link_name,
      modular::ModuleManifestPtr module_manifest,
      CreateChainInfoPtr create_chain_info, const ModuleSource module_source,
      SurfaceRelationPtr surface_relation,
      f1dl::InterfaceRequest<component::ServiceProvider> incoming_services,
      f1dl::InterfaceRequest<ModuleController> module_controller_request,
      f1dl::InterfaceHandle<EmbedModuleWatcher> embed_module_watcher,
      fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
      DaisyPtr daisy,
      ResultCall result_call)

      : Operation("StoryControllerImpl::StartModuleCall", container,
                  std::move(result_call), module_url),
        story_controller_impl_(story_controller_impl),
        module_path_(module_path.Clone()),
        module_url_(module_url),
        link_name_(link_name),
        module_manifest_(std::move(module_manifest)),
        create_chain_info_(std::move(create_chain_info)),
        module_source_(module_source),
        surface_relation_(std::move(surface_relation)),
        incoming_services_(std::move(incoming_services)),
        module_controller_request_(std::move(module_controller_request)),
        embed_module_watcher_(std::move(embed_module_watcher)),
        view_owner_request_(std::move(view_owner_request)),
        daisy_(std::move(daisy)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    // We currently require a 1:1 relationship between module
    // application instances and Module service instances, because
    // flutter only allows one ViewOwner per flutter application,
    // and we need one ViewOwner instance per Module instance.

    // If this is a root Module and we don't have a link name, give it a root
    // link of the same name.
    if (!link_name_ && ParentModulePath(module_path_)->empty()) {
      link_name_ = module_path_->at(0);
    }

    // TODO(thatguy): Remove any root link initialization. MI4-739
    if (link_name_) {
      link_path_ = LinkPath::New();
      link_path_->module_path = ParentModulePath(module_path_);
      link_path_->link_name = link_name_;
      InitializeModuleData(flow);
    } else {
      // If the link name is null, this module receives the default link of
      // its parent module. We need to retrieve which one it is from story
      // storage.
      new ReadDataCall<ModuleData>(
          &operation_queue_, story_controller_impl_->page(),
          MakeModuleKey(ParentModulePath(module_path_)),
          false /* not_found_is_ok */, XdrModuleData,
          [this, flow](ModuleDataPtr module_data) {
            FXL_DCHECK(module_data);
            link_path_ = module_data->link_path.Clone();
            InitializeModuleData(flow);
          });
    }
  }

  void InitializeModuleData(FlowToken flow) {
    module_data_ = ModuleData::New();
    module_data_->module_url = module_url_;
    module_data_->module_path = module_path_.Clone();
    module_data_->link_path = link_path_.Clone();
    module_data_->module_source = module_source_;
    module_data_->surface_relation = surface_relation_.Clone();
    module_data_->module_stopped = false;
    module_data_->daisy = std::move(daisy_);

    // Initialize |module_data_->chain_data|.
    new InitializeChainCall(&operation_queue_, story_controller_impl_,
                            module_path_.Clone(), create_chain_info_.Clone(),
                            [this, flow](ChainDataPtr chain_data) {
                              module_data_->chain_data = std::move(chain_data);
                              MaybeWriteModuleData(flow);
                            });
  }

  void MaybeWriteModuleData(FlowToken flow) {
    // We check if the data in the ledger is already what we want. If so, we do
    // nothing.
    // Read the module data.
    new ReadDataCall<ModuleData>(
        &operation_queue_, story_controller_impl_->page(),
        MakeModuleKey(module_path_), true /* not_found_is_ok */, XdrModuleData,
        [this, flow](ModuleDataPtr data) {
          // If what we're about to write is already present on the ledger, just
          // launch the module.
          if (data.Equals(module_data_)) {
            Launch(flow);
            return;
          }
          WriteModuleData(flow);
        });
  }

  void WriteModuleData(FlowToken flow) {
    std::string key{MakeModuleKey(module_path_)};
    new BlockingModuleDataWriteCall(&operation_queue_, story_controller_impl_,
                                    std::move(key), module_data_.Clone(),
                                    [this, flow] { Launch(flow); });
  }

  void Launch(FlowToken flow) {
    new LaunchModuleCall(&operation_queue_, story_controller_impl_,
                         std::move(module_data_), std::move(incoming_services_),
                         std::move(module_controller_request_),
                         std::move(embed_module_watcher_),
                         std::move(view_owner_request_), [flow] {});
  }

  // Passed in:
  StoryControllerImpl* const story_controller_impl_;  // not owned
  const f1dl::VectorPtr<f1dl::StringPtr> module_path_;
  const f1dl::StringPtr module_url_;
  f1dl::StringPtr link_name_;
  ModuleManifestPtr module_manifest_;
  CreateChainInfoPtr create_chain_info_;
  const ModuleSource module_source_;
  const SurfaceRelationPtr surface_relation_;
  f1dl::InterfaceRequest<component::ServiceProvider> incoming_services_;
  f1dl::InterfaceRequest<ModuleController> module_controller_request_;
  f1dl::InterfaceHandle<EmbedModuleWatcher> embed_module_watcher_;
  fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request_;
  DaisyPtr daisy_;

  LinkPathPtr link_path_;
  ModuleDataPtr module_data_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StartModuleCall);
};

class StoryControllerImpl::StartModuleInShellCall : Operation<> {
 public:
  StartModuleInShellCall(
      OperationContainer* const container,
      StoryControllerImpl* const story_controller_impl,
      const f1dl::VectorPtr<f1dl::StringPtr>& module_path,
      const f1dl::StringPtr& module_url, const f1dl::StringPtr& link_name,
      modular::ModuleManifestPtr module_manifest,
      CreateChainInfoPtr create_chain_info,
      f1dl::InterfaceRequest<component::ServiceProvider> incoming_services,
      f1dl::InterfaceRequest<ModuleController> module_controller_request,
      SurfaceRelationPtr surface_relation, const bool focus,
      ModuleSource module_source, DaisyPtr daisy, ResultCall result_call)
      : Operation("StoryControllerImpl::StartModuleInShellCall", container,
                  std::move(result_call), module_url),
        story_controller_impl_(story_controller_impl),
        module_path_(module_path.Clone()),
        module_url_(module_url),
        link_name_(link_name),
        module_manifest_(std::move(module_manifest)),
        create_chain_info_(std::move(create_chain_info)),
        incoming_services_(std::move(incoming_services)),
        module_controller_request_(std::move(module_controller_request)),
        surface_relation_(std::move(surface_relation)),
        focus_(focus),
        module_source_(module_source),
        daisy_(std::move(daisy)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    // TODO(mesch): The StartModuleCall may result in just a new
    // ModuleController connection to an existing
    // ModuleControllerImpl. In that case, the view owner request is
    // closed, and the view owner should not be sent to the story
    // shell.
    new StartModuleCall(
        &operation_queue_, story_controller_impl_, module_path_, module_url_,
        link_name_, module_manifest_.Clone(), create_chain_info_.Clone(),
        module_source_, surface_relation_.Clone(),
        std::move(incoming_services_), std::move(module_controller_request_),
        nullptr /* embed_module_watcher */, view_owner_.NewRequest(),
        std::move(daisy_), [this, flow] { Cont(flow); });
  }

  void Cont(FlowToken flow) {
    // If this is called during Stop(), story_shell_ might already have been
    // reset. TODO(mesch): Then the whole operation should fail.
    if (!story_controller_impl_->story_shell_) {
      return;
    }

    // We only add a module to story shell if its either a root module or its
    // anchor is already known to story shell.

    if (module_path_->size() == 1) {
      ConnectView(flow, "");
      return;
    }

    auto* const connection =
        story_controller_impl_->FindConnection(module_path_);
    FXL_CHECK(connection);  // Was just created.

    Connection* const embedder =
        story_controller_impl_->FindEmbedder(module_path_);
    if (embedder) {
      embedder->embed_module_watcher->OnStartModuleInShell(
          connection->module_controller_impl->NewEmbedModuleController());
    }

    auto* const anchor = story_controller_impl_->FindAnchor(connection);
    if (anchor) {
      const auto anchor_view_id = PathString(anchor->module_data->module_path);
      if (story_controller_impl_->connected_views_.count(anchor_view_id)) {
        ConnectView(flow, anchor_view_id);
        return;
      }
    }

    story_controller_impl_->pending_views_.emplace(std::make_pair(
        PathString(module_path_),
        std::make_pair(module_path_.Clone(), std::move(view_owner_))));
  }

  void ConnectView(FlowToken flow, const f1dl::StringPtr& anchor_view_id) {
    const auto view_id = PathString(module_path_);

    // If there is no anchor view id, arbitrarily choose a different module as
    // the parent.
    // TODO(MI4-889): Pass along more useful layout signals to the story shell.
    f1dl::StringPtr parent_id = anchor_view_id;
    if (anchor_view_id->empty()) {
      const std::string first_module_string =
          PathString(story_controller_impl_->first_module_path_);
      if (view_id != first_module_string) {
        parent_id = first_module_string;
      }
    }

    story_controller_impl_->story_shell_->ConnectView(
        std::move(view_owner_), view_id, parent_id,
        std::move(surface_relation_), std::move(module_manifest_));

    story_controller_impl_->connected_views_.emplace(view_id);
    story_controller_impl_->ProcessPendingViews();

    if (focus_) {
      story_controller_impl_->story_shell_->FocusView(view_id, anchor_view_id);
    }
  }

  StoryControllerImpl* const story_controller_impl_;
  const f1dl::VectorPtr<f1dl::StringPtr> module_path_;
  const f1dl::StringPtr module_url_;
  const f1dl::StringPtr link_name_;
  ModuleManifestPtr module_manifest_;
  CreateChainInfoPtr create_chain_info_;
  f1dl::InterfaceRequest<component::ServiceProvider> incoming_services_;
  f1dl::InterfaceRequest<ModuleController> module_controller_request_;
  SurfaceRelationPtr surface_relation_;
  const bool focus_;
  const ModuleSource module_source_;
  DaisyPtr daisy_;

  ModuleControllerPtr module_controller_;
  views_v1_token::ViewOwnerPtr view_owner_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StartModuleInShellCall);
};

class StoryControllerImpl::AddModuleCall : Operation<> {
 public:
  AddModuleCall(OperationContainer* const container,
                StoryControllerImpl* const story_controller_impl,
                f1dl::VectorPtr<f1dl::StringPtr> module_path,
                const f1dl::StringPtr& module_url,
                const f1dl::StringPtr& link_name,
                SurfaceRelationPtr surface_relation,
                const ResultCall& done)
      : Operation("StoryControllerImpl::AddModuleCall",
                  container,
                  done,
                  module_url),
        story_controller_impl_(story_controller_impl),
        module_path_(std::move(module_path)),
        module_url_(module_url),
        link_name_(link_name),
        surface_relation_(std::move(surface_relation)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};
    link_path_ = LinkPath::New();
    link_path_->module_path = ParentModulePath(module_path_);
    link_path_->link_name = link_name_;

    WriteModuleData(flow);
  }

  void WriteModuleData(FlowToken flow) {
    module_data_ = ModuleData::New();
    module_data_->module_url = module_url_;
    module_data_->module_path = module_path_.Clone();
    module_data_->link_path = link_path_.Clone();
    module_data_->module_source = ModuleSource::EXTERNAL;
    module_data_->surface_relation = surface_relation_.Clone();
    module_data_->module_stopped = false;

    // TODO: Initialize |module_data_->chain_data|. This call is only used
    // for operations on StoryController, which don't yet accept
    // CreateChainInfo.
    module_data_->chain_data = ChainData::New();
    module_data_->chain_data->key_to_link_map.resize(0);

    std::string key{MakeModuleKey(module_path_)};
    new BlockingModuleDataWriteCall(&operation_queue_, story_controller_impl_,
                                    std::move(key), module_data_.Clone(),
                                    [this, flow] { Cont(flow); });
  }

  void Cont(FlowToken flow) {
    if (story_controller_impl_->IsRunning()) {
      // TODO(jphsiao): Figure out what to do for manifest here.
      new StartModuleInShellCall(
          &operation_queue_, story_controller_impl_, module_path_, module_url_,
          link_name_, nullptr /* module_manifest */, nullptr /* chain_data */,
          nullptr /* incoming_services */,
          nullptr /* module_controller_request*/, std::move(surface_relation_),
          true, ModuleSource::EXTERNAL, std::move(module_data_->daisy),
          [flow] {});
    }
  }

  StoryControllerImpl* const story_controller_impl_;
  const f1dl::VectorPtr<f1dl::StringPtr> module_path_;
  const f1dl::StringPtr module_url_;
  const f1dl::StringPtr link_name_;
  SurfaceRelationPtr surface_relation_;

  LinkPathPtr link_path_;
  ModuleDataPtr module_data_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AddModuleCall);
};

class StoryControllerImpl::AddForCreateCall : Operation<> {
 public:
  AddForCreateCall(OperationContainer* const container,
                   StoryControllerImpl* const story_controller_impl,
                   const f1dl::StringPtr& module_name,
                   const f1dl::StringPtr& module_url,
                   const f1dl::StringPtr& link_name,
                   CreateLinkInfoPtr create_link_info,
                   const ResultCall& done)
      : Operation("StoryControllerImpl::AddForCreateCall",
                  container,
                  done,
                  module_url),
        story_controller_impl_(story_controller_impl),
        module_name_(module_name),
        module_url_(module_url),
        link_name_(link_name),
        create_link_info_(std::move(create_link_info)) {
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

    if (!create_link_info_.is_null()) {
      // There is no module path; this link exists outside the scope of a
      // module.
      LinkPathPtr link_path = LinkPath::New();
      link_path->module_path = f1dl::VectorPtr<f1dl::StringPtr>::New(0);
      link_path->link_name = link_name_;
      new ConnectLinkCall(
          &operation_queue_, story_controller_impl_, std::move(link_path),
          LinkImpl::ConnectionType::Primary, std::move(create_link_info_),
          true /* notify_watchers */, link_.NewRequest(), [flow] {});
    }

    auto module_path = f1dl::VectorPtr<f1dl::StringPtr>::New(0);
    module_path.push_back(module_name_);

    // This is a top level module, which therefore is not embedded. So the
    // SurfaceRelation is indeed default and not null.
    new AddModuleCall(&operation_queue_, story_controller_impl_,
                      std::move(module_path), module_url_, link_name_,
                      SurfaceRelation::New(), [flow] {});
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const f1dl::StringPtr module_name_;
  const f1dl::StringPtr module_url_;
  const f1dl::StringPtr link_name_;
  CreateLinkInfoPtr create_link_info_;

  LinkPtr link_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AddForCreateCall);
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
    // At this point, we don't need to monitor the root modules for state
    // changes anymore, because the next state change of the story is triggered
    // by the Cleanup() call below.
    story_controller_impl_->track_root_module_state_ = false;

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
      story_controller_impl_->story_shell_app_->Teardown(
          kBasicTimeout, [this] { StoryShellDown(); });
    } else {
      StoryShellDown();
    }
  }

  void StoryShellDown() {
    story_controller_impl_->story_shell_app_.reset();
    story_controller_impl_->story_shell_.Unbind();
    if (story_controller_impl_->story_context_binding_.is_bound()) {
      // Close() dchecks if called while not bound.
      story_controller_impl_->story_context_binding_.Unbind();
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
                 const f1dl::VectorPtr<f1dl::StringPtr>& module_path,
                 const std::function<void()>& done)
      : Operation("StoryControllerImpl::StopModuleCall", container, done),
        story_controller_impl_(story_controller_impl),
        module_path_(module_path.Clone()) {
    Ready();
  }

 private:
  void Run() override {
    // NOTE(alhaad): We don't use flow tokens here. See NOTE in Kill() to know
    // why.

    // Read the module data.
    new ReadDataCall<ModuleData>(
        &operation_queue_, story_controller_impl_->page(),
        MakeModuleKey(module_path_), false /* not_found_is_ok */, XdrModuleData,
        [this](ModuleDataPtr data) {
          module_data_ = std::move(data);
          Cont1();
        });
  }

  void Cont1() {
    // If the module is already marked as stopped, kill module.
    if (module_data_->module_stopped) {
      Kill();
      return;
    }

    // Write the module data back, with module_stopped = true, which is a
    // global state shared between machines to track when the module is
    // explicitly stopped.
    module_data_->module_stopped = true;

    std::string key{MakeModuleKey(module_data_->module_path)};
    // TODO(alhaad: This call may never continue if the data we're writing to
    // the ledger is the same as the data already in there as that will not
    // trigger an OnPageChange().
    new BlockingModuleDataWriteCall(&operation_queue_, story_controller_impl_,
                                    std::move(key), module_data_->Clone(),
                                    [this] { Kill(); });
  }

  void Kill() {
    new KillModuleCall(&operation_queue_, story_controller_impl_,
                       std::move(module_data_), [this] {
                         // NOTE(alhaad): An interesting flow of control to keep
                         // in mind:
                         // 1. From ModuleController.Stop() which can only be
                         // called from FIDL, we call
                         // StoryControllerImpl.StopModule().
                         // 2. StoryControllerImpl.StopModule() pushes
                         // StopModuleCall onto the operation queue.
                         // 3. When operation becomes current, we write to
                         // ledger, block and continue on receiving OnPageChange
                         // from ledger.
                         // 4. We then call KillModuleCall on a sub operation
                         // queue.
                         // 5. KillModuleCall will call Teardown() on the same
                         // ModuleControllerImpl that had started
                         // ModuleController.Stop(). In the callback from
                         // Teardown(), it calls done_() (and NOT Done()).
                         // 6. done_() in KillModuleCall leads to the next line
                         // here, which calls Done() which would call the FIDL
                         // callback from ModuleController.Stop().
                         // 7. Done() on the next line also deletes this which
                         // deletes the still running KillModuleCall, but this
                         // is okay because the only thing that was left to do
                         // in KillModuleCall was FlowToken going out of scope.
                         Done();
                       });
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const f1dl::VectorPtr<f1dl::StringPtr> module_path_;
  ModuleDataPtr module_data_;

  OperationQueue operation_queue_;

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

class StoryControllerImpl::StartCall : Operation<> {
 public:
  StartCall(OperationContainer* const container,
            StoryControllerImpl* const story_controller_impl,
            fidl::InterfaceRequest<views_v1_token::ViewOwner> request)
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

    story_controller_impl_->track_root_module_state_ = true;
    story_controller_impl_->StartStoryShell(std::move(request_));

    // Start *all* the root modules, not just the first one, with their
    // respective links.
    new ReadAllDataCall<ModuleData>(
        &operation_queue_, story_controller_impl_->page(), kModuleKeyPrefix,
        XdrModuleData, [this, flow](f1dl::VectorPtr<ModuleDataPtr> data) {
          for (auto& module_data : *data) {
            if (module_data->module_source == ModuleSource::EXTERNAL &&
                !module_data->module_stopped) {
              new StartModuleInShellCall(
                  &operation_queue_, story_controller_impl_,
                  module_data->module_path, module_data->module_url,
                  module_data->link_path->link_name,
                  nullptr /* module_manifest */, nullptr /* chain_data */,
                  nullptr /* incoming_services */,
                  nullptr /* module_controller_request */,
                  module_data->surface_relation.Clone(), true,
                  module_data->module_source, std::move(module_data->daisy),
                  [flow] {});
            }
          }

          story_controller_impl_->state_ = StoryState::STARTING;
          story_controller_impl_->NotifyStateChange();
        });
  };

  StoryControllerImpl* const story_controller_impl_;  // not owned
  fidl::InterfaceRequest<views_v1_token::ViewOwner> request_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StartCall);
};

class StoryControllerImpl::LedgerNotificationCall : Operation<> {
 public:
  LedgerNotificationCall(OperationContainer* const container,
                         StoryControllerImpl* const story_controller_impl,
                         ModuleDataPtr module_data)
      : Operation("StoryControllerImpl::LedgerNotificationCall",
                  container,
                  [] {}),
        story_controller_impl_(story_controller_impl),
        module_data_(std::move(module_data)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};
    if (!story_controller_impl_->IsRunning() ||
        module_data_->module_source != ModuleSource::EXTERNAL) {
      return;
    }

    // Check for existing module at the given path.
    auto* const i =
        story_controller_impl_->FindConnection(module_data_->module_path);
    if (i && module_data_->module_stopped) {
      new KillModuleCall(&operation_queue_, story_controller_impl_,
                         std::move(module_data_), [flow] {});
      return;
    } else if (module_data_->module_stopped) {
      // There is no module running, and the ledger change is for a stopped
      // module so do nothing.
      return;
    }

    // We reach this point only if we want to start an external module.
    new StartModuleInShellCall(
        &operation_queue_, story_controller_impl_, module_data_->module_path,
        module_data_->module_url, module_data_->link_path->link_name,
        nullptr /* module_manifest */, nullptr /* chain_data */,
        nullptr /* incoming_services */,
        nullptr /* module_controller_request */,
        std::move(module_data_->surface_relation), true,
        module_data_->module_source, std::move(module_data_->daisy), [flow] {});
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;  // not owned
  ModuleDataPtr module_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerNotificationCall);
};

class StoryControllerImpl::FocusCall : Operation<> {
 public:
  FocusCall(OperationContainer* const container,
            StoryControllerImpl* const story_controller_impl,
            f1dl::VectorPtr<f1dl::StringPtr> module_path)
      : Operation("StoryControllerImpl::FocusCall", container, [] {}),
        story_controller_impl_(story_controller_impl),
        module_path_(std::move(module_path)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    if (!story_controller_impl_->story_shell_) {
      return;
    }

    Connection* const anchor = story_controller_impl_->FindAnchor(
        story_controller_impl_->FindConnection(module_path_));
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
  const f1dl::VectorPtr<f1dl::StringPtr> module_path_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FocusCall);
};

class StoryControllerImpl::DefocusCall : Operation<> {
 public:
  DefocusCall(OperationContainer* const container,
              StoryControllerImpl* const story_controller_impl,
              f1dl::VectorPtr<f1dl::StringPtr> module_path)
      : Operation("StoryControllerImpl::DefocusCall", container, [] {}),
        story_controller_impl_(story_controller_impl),
        module_path_(std::move(module_path)) {
    Ready();
  }

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
  const f1dl::VectorPtr<f1dl::StringPtr> module_path_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DefocusCall);
};

class StoryControllerImpl::ResolveModulesCall
    : Operation<FindModulesResultPtr> {
 public:
  // If |daisy| originated from a Module, |requesting_module_path| must be
  // non-null.  Otherwise, it is an error for the |daisy| to have any Nouns of
  // type 'link_name' (since a Link with a link name without an associated
  // Module path is impossible to locate).
  ResolveModulesCall(OperationContainer* const container,
                     StoryControllerImpl* const story_controller_impl,
                     DaisyPtr daisy,
                     f1dl::VectorPtr<f1dl::StringPtr> requesting_module_path,
                     ResultCall result_call)
      : Operation("StoryControllerImpl::ResolveModulesCall",
                  container,
                  std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        daisy_(std::move(daisy)),
        requesting_module_path_(std::move(requesting_module_path)) {
    Ready();
  }

 private:
  void Run() {
    FlowToken flow{this, &result_};

    DaisyToResolverQuery([this, flow] { Cont(flow); });
  }

  void DaisyToResolverQuery(std::function<void()> cont) {
    resolver_query_ = ResolverQuery::New();
    resolver_query_->verb = daisy_->verb;
    resolver_query_->url = daisy_->url;

    std::shared_ptr<int> outstanding_requests{new int{0}};
    for (const auto& entry : *daisy_->nouns) {
      const auto& name = entry->name;
      const auto& noun = entry->noun;

      if (noun->is_json()) {
        auto noun_constraint = ResolverNounConstraint::New();
        noun_constraint->set_json(noun->get_json());
        auto noun_constraint_entry = ResolverNounConstraintEntry::New();
        noun_constraint_entry->key = name;
        noun_constraint_entry->constraint = std::move(noun_constraint);
        resolver_query_->noun_constraints.push_back(
            std::move(noun_constraint_entry));

      } else if (noun->is_link_name() || noun->is_link_path()) {
        // Find the chain for this Module, or use the one that was provided via
        // the noun.
        auto link_path =
            noun->is_link_path()
                ? noun->get_link_path().Clone()
                : story_controller_impl_->GetLinkPathForChainKey(
                      requesting_module_path_, noun->get_link_name());

        if (!link_path) {
          // The chain doesn't contain a value for this Link, so assume it's
          // one the Module created itself.
          link_path = LinkPath::New();
          link_path->module_path = requesting_module_path_.Clone();
          link_path->link_name = noun->get_link_name();
        }

        ++(*outstanding_requests);
        LinkPtr link;
        new ConnectLinkCall(
            &operation_queue_, story_controller_impl_, link_path.Clone(),
            LinkImpl::ConnectionType::Secondary, nullptr /* create_link_info */,
            false /* notify_watchers */, link.NewRequest(), fxl::MakeCopyable([
              this, name, outstanding_requests, cont,
              link_path = std::move(link_path), link = std::move(link)
            ]() mutable {
              link->Get(
                  nullptr /* path */, fxl::MakeCopyable([
                    this, name, outstanding_requests, cont,
                    link_path = std::move(link_path), link = std::move(link)
                  ](const f1dl::StringPtr& content) mutable {
                    auto link_info = ResolverLinkInfo::New();
                    link_info->path = std::move(link_path);
                    link_info->content_snapshot = content;

                    auto noun_constraint = ResolverNounConstraint::New();
                    noun_constraint->set_link_info(std::move(link_info));

                    auto noun_constraint_entry =
                        ResolverNounConstraintEntry::New();
                    noun_constraint_entry->key = name;
                    noun_constraint_entry->constraint =
                        std::move(noun_constraint);
                    resolver_query_->noun_constraints.push_back(
                        std::move(noun_constraint_entry));

                    --(*outstanding_requests);
                    if (*outstanding_requests == 0) {
                      cont();
                    }
                  }));
            }));

      } else if (noun->is_entity_type()) {
        auto noun_constraint = ResolverNounConstraint::New();
        noun_constraint->set_entity_type(noun->get_entity_type().Clone());
        auto noun_constraint_entry = ResolverNounConstraintEntry::New();
        noun_constraint_entry->key = name;
        noun_constraint_entry->constraint = std::move(noun_constraint);
        resolver_query_->noun_constraints.push_back(
            std::move(noun_constraint_entry));

      } else if (noun->is_entity_reference()) {
        auto noun_constraint = ResolverNounConstraint::New();
        noun_constraint->set_entity_reference(noun->get_entity_reference());
        auto noun_constraint_entry = ResolverNounConstraintEntry::New();
        noun_constraint_entry->key = name;
        noun_constraint_entry->constraint = std::move(noun_constraint);
        resolver_query_->noun_constraints.push_back(
            std::move(noun_constraint_entry));
      }
    }

    if (*outstanding_requests == 0) {
      cont();
    }
  }

  void Cont(FlowToken flow) {
    story_controller_impl_->story_provider_impl_->module_resolver()
        ->FindModules(std::move(resolver_query_), nullptr,
                      [this, flow](const FindModulesResultPtr& result) {
                        result_ = result.Clone();
                      });
  }

  OperationQueue operation_queue_;

  StoryControllerImpl* const story_controller_impl_;  // not owned
  const DaisyPtr daisy_;
  const f1dl::VectorPtr<f1dl::StringPtr> requesting_module_path_;

  ResolverQueryPtr resolver_query_;
  FindModulesResultPtr result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ResolveModulesCall);
};

class StoryControllerImpl::StartContainerInShellCall : Operation<> {
 public:
  StartContainerInShellCall(
      OperationContainer* const container,
      StoryControllerImpl* const story_controller_impl,
      f1dl::VectorPtr<f1dl::StringPtr> parent_module_path,
      const f1dl::StringPtr& container_name,
      SurfaceRelationPtr parent_relation,
      f1dl::VectorPtr<ContainerLayoutPtr> layout,
      f1dl::VectorPtr<ContainerRelationEntryPtr> relationships,
      f1dl::VectorPtr<ContainerNodePtr> nodes)
      : Operation("StoryControllerImpl::StartContainerInShellCall",
                  container,
                  [] {}),
        story_controller_impl_(story_controller_impl),
        parent_module_path_(std::move(parent_module_path)),
        container_name_(container_name),
        parent_relation_(std::move(parent_relation)),
        layout_(std::move(layout)),
        relationships_(std::move(relationships)),
        nodes_(std::move(nodes)) {
    Ready();

    for (auto& relationship : *relationships_) {
      relation_map_[relationship->node_name] = relationship.Clone();
    }
  }

 private:
  void Run() override {
    FlowToken flow{this};
    // parent + container used as module path of requesting module for
    // containers
    f1dl::VectorPtr<f1dl::StringPtr> module_path = parent_module_path_.Clone();
    // module_path.push_back(container_name_);
    // Adding non-module 'container_name_' to the module path results in
    // Ledger Client issuing a ReadData() call and failing with a fatal error
    // when module_data cannot be found
    // TODO(djmurphy): follow up, probably make containers modules
    for (size_t i = 0; i < nodes_->size(); ++i) {
      new ResolveModulesCall(&operation_queue_, story_controller_impl_,
                             nodes_->at(i)->daisy.Clone(),
                             std::move(module_path),  // May be wrong.
                             [this, flow, i](FindModulesResultPtr result) {
                               Cont(flow, i, std::move(result));
                             });
    }
  }

  void Cont(FlowToken flow, const size_t i, FindModulesResultPtr result) {
    if (result->modules->size() > 0) {
      // We just run the first module in story shell.
      // TODO(alhaad/thatguy): Revisit the assumption.
      const auto& module_result = result->modules->at(0);
      views_v1_token::ViewOwnerPtr view_owner;
      node_views_[nodes_->at(i)->node_name] = std::move(view_owner);
      f1dl::VectorPtr<f1dl::StringPtr> module_path = parent_module_path_.Clone();
      // module_path.push_back(container_name_);
      // same issue as documented in Run()
      module_path.push_back(nodes_->at(i)->node_name);
      new StartModuleCall(
          &operation_queue_, story_controller_impl_, std::move(module_path),
          module_result->module_id, nullptr /* link_name */,
          nullptr /* module_manifest */,
          module_result->create_chain_info.Clone(), ModuleSource::INTERNAL,
          std::move(relation_map_[nodes_->at(i)->node_name]->relationship),
          nullptr /* incoming_services */,
          nullptr /* module_controller_request */,
          nullptr /* embed_module_watcher */,
          node_views_[nodes_->at(i)->node_name].NewRequest(),
          nullptr /* daisy */,
          [this, flow] { Cont2(flow); });
    } else {
      Cont2(flow);
    }
  }

  void Cont2(FlowToken flow) {
    nodes_done_++;

    if (nodes_done_ < nodes_->size()) {
      return;
    }
    if (!story_controller_impl_->story_shell_) {
      return;
    }
    auto views = f1dl::VectorPtr<modular::ContainerViewPtr>::New(nodes_->size());
    for (size_t i = 0; i < nodes_->size(); i++) {
      ContainerViewPtr view = ContainerView::New();
      view->node_name = nodes_->at(i)->node_name;
      view->owner = std::move(node_views_[nodes_->at(i)->node_name]);
      views->at(i) = std::move(view);
    }
    story_controller_impl_->story_shell_->AddContainer(
        container_name_, PathString(parent_module_path_),
        std::move(parent_relation_), std::move(layout_),
        std::move(relationships_), std::move(views));
  }

  StoryControllerImpl* const story_controller_impl_;  // not owned
  OperationQueue operation_queue_;
  const f1dl::VectorPtr<f1dl::StringPtr> parent_module_path_;
  const f1dl::StringPtr container_name_;

  SurfaceRelationPtr parent_relation_;
  f1dl::VectorPtr<ContainerLayoutPtr> layout_;
  f1dl::VectorPtr<ContainerRelationEntryPtr> relationships_;
  const f1dl::VectorPtr<ContainerNodePtr> nodes_;
  std::map<std::string, ContainerRelationEntryPtr> relation_map_;
  size_t nodes_done_{};

  // map of node_name to view_owners
  std::map<f1dl::StringPtr, views_v1_token::ViewOwnerPtr> node_views_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StartContainerInShellCall);
};

// An operation that first performs module resolution with the provided Daisy
// and subsequently starts the most appropriate resolved module in the story
// shell.
class StoryControllerImpl::AddDaisyCall : Operation<StartModuleStatus> {
 public:
  AddDaisyCall(
      OperationContainer* const container,
      StoryControllerImpl* const story_controller_impl,
      f1dl::VectorPtr<f1dl::StringPtr> requesting_module_path,
      const std::string& module_name, DaisyPtr daisy,
      f1dl::InterfaceRequest<component::ServiceProvider> incoming_services,
      f1dl::InterfaceRequest<ModuleController> module_controller_request,
      SurfaceRelationPtr surface_relation,
      fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
      const ModuleSource module_source,
      ResultCall result_call)
      : Operation("StoryControllerImpl::AddDaisyCall",
                  container,
                  std::move(result_call)),
        story_controller_impl_(story_controller_impl),
        requesting_module_path_(std::move(requesting_module_path)),
        module_name_(module_name),
        daisy_(std::move(daisy)),
        incoming_services_(std::move(incoming_services)),
        module_controller_request_(std::move(module_controller_request)),
        surface_relation_(std::move(surface_relation)),
        view_owner_request_(std::move(view_owner_request)),
        module_source_(module_source) {
    Ready();
  }

 private:
  void Run() {
    FlowToken flow{this, &result_};

    new ResolveModulesCall(&operation_queue_, story_controller_impl_,
                           daisy_.Clone(), requesting_module_path_.Clone(),
                           [this, flow](FindModulesResultPtr result) {
                             StartModuleFromResult(flow, std::move(result));
                           });
  }

  void StartModuleFromResult(FlowToken flow, FindModulesResultPtr result) {
    if (!result->modules->empty()) {
      // Runs the first module in story shell.
      const auto& module_result = result->modules->at(0);
      const auto& manifest = module_result->manifest;
      const auto& module_url = module_result->module_id;
      const auto& create_chain_info = module_result->create_chain_info;

      auto module_path = requesting_module_path_.Clone();
      module_path.push_back(module_name_);

      if (!view_owner_request_) {
        new StartModuleInShellCall(
            &operation_queue_, story_controller_impl_, std::move(module_path),
            module_url, nullptr /* link_name */, manifest.Clone(),
            create_chain_info.Clone(), std::move(incoming_services_),
            std::move(module_controller_request_), std::move(surface_relation_),
            true /* focus */, module_source_, std::move(daisy_), [flow] {});
      } else {
        new StartModuleCall(
            &operation_queue_, story_controller_impl_, std::move(module_path),
            module_url, nullptr /* link_name */, manifest.Clone(),
            create_chain_info.Clone(), module_source_,
            std::move(surface_relation_), std::move(incoming_services_),
            std::move(module_controller_request_),
            nullptr /* embed_module_watcher */, std::move(view_owner_request_),
            std::move(daisy_), [flow] {});
      }

      result_ = StartModuleStatus::SUCCESS;
    }
  }

  OperationQueue operation_queue_;
  StoryControllerImpl* const story_controller_impl_;
  f1dl::VectorPtr<f1dl::StringPtr> requesting_module_path_;
  const std::string module_name_;
  DaisyPtr daisy_;
  f1dl::InterfaceRequest<component::ServiceProvider> incoming_services_;
  f1dl::InterfaceRequest<ModuleController> module_controller_request_;
  SurfaceRelationPtr surface_relation_;
  fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request_;
  const ModuleSource module_source_;

  StartModuleStatus result_{StartModuleStatus::NO_MODULES_FOUND};

  FXL_DISALLOW_COPY_AND_ASSIGN(AddDaisyCall);
};

StoryControllerImpl::StoryControllerImpl(
    const f1dl::StringPtr& story_id,
    LedgerClient* const ledger_client,
    LedgerPageId story_page_id,
    StoryProviderImpl* const story_provider_impl)
    : PageClient(MakeStoryKey(story_id),
                 ledger_client,
                 story_page_id.Clone(),
                 kModuleKeyPrefix),
      story_id_(story_id),
      story_provider_impl_(story_provider_impl),
      ledger_client_(ledger_client),
      story_page_id_(std::move(story_page_id)),
      story_scope_(story_provider_impl_->user_scope(),
                   kStoryScopeLabelPrefix + story_id_.get()),
      story_context_binding_(this),
      story_marker_impl_(new StoryMarkerImpl) {
  story_scope_.AddService<StoryMarker>(
      [this](f1dl::InterfaceRequest<StoryMarker> request) {
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
      [this](f1dl::InterfaceRequest<maxwell::ContextWriter> request) {
        intelligence_services_->GetContextWriter(std::move(request));
      });
}

StoryControllerImpl::~StoryControllerImpl() = default;

void StoryControllerImpl::Connect(
    f1dl::InterfaceRequest<StoryController> request) {
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

void StoryControllerImpl::AddForCreate(const f1dl::StringPtr& module_name,
                                       const f1dl::StringPtr& module_url,
                                       const f1dl::StringPtr& link_name,
                                       CreateLinkInfoPtr create_link_info,
                                       const std::function<void()>& done) {
  if (!module_url) {
    done();
  }

  new AddForCreateCall(&operation_queue_, this, module_name, module_url,
                       link_name, std::move(create_link_info), done);
}

StoryState StoryControllerImpl::GetStoryState() const {
  return state_;
}

void StoryControllerImpl::Sync(const std::function<void()>& done) {
  new SyncCall(&operation_queue_, done);
}

void StoryControllerImpl::FocusModule(
    const f1dl::VectorPtr<f1dl::StringPtr>& module_path) {
  new FocusCall(&operation_queue_, this, module_path.Clone());
}

void StoryControllerImpl::DefocusModule(
    const f1dl::VectorPtr<f1dl::StringPtr>& module_path) {
  new DefocusCall(&operation_queue_, this, module_path.Clone());
}

void StoryControllerImpl::StopModule(
    const f1dl::VectorPtr<f1dl::StringPtr>& module_path,
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
  pending_views_.erase(PathString(f->module_data->module_path));
  connections_.erase(f);
}

const f1dl::StringPtr& StoryControllerImpl::GetStoryId() const {
  return story_id_;
}

void StoryControllerImpl::RequestStoryFocus() {
  story_provider_impl_->RequestStoryFocus(story_id_);
}

void StoryControllerImpl::ConnectLinkPath(
    LinkPathPtr link_path,
    LinkImpl::ConnectionType connection_type,
    f1dl::InterfaceRequest<Link> request) {
  new ConnectLinkCall(&operation_queue_, this, std::move(link_path),
                      connection_type, nullptr /* create_link_info */,
                      true /* notify_watchers */, std::move(request), [] {});
}

LinkPathPtr StoryControllerImpl::GetLinkPathForChainKey(
    const f1dl::VectorPtr<f1dl::StringPtr>& module_path,
    const f1dl::StringPtr& key) {
  auto i = std::find_if(chains_.begin(), chains_.end(),
                        [&module_path](const std::unique_ptr<ChainImpl>& ptr) {
                          return ptr->chain_path().Equals(module_path);
                        });
  // We expect a Chain for each Module to have been created during Module
  // initialization.
  FXL_CHECK(i != chains_.end()) << PathString(module_path);

  return (*i)->GetLinkPathForKey(key);
}

void StoryControllerImpl::StartModuleDeprecated(
    const f1dl::VectorPtr<f1dl::StringPtr>& parent_module_path,
    const f1dl::StringPtr& module_name, const f1dl::StringPtr& module_url,
    const f1dl::StringPtr& link_name, const modular::ModuleManifestPtr manifest,
    CreateChainInfoPtr create_chain_info,
    f1dl::InterfaceRequest<component::ServiceProvider> incoming_services,
    f1dl::InterfaceRequest<ModuleController> module_controller_request,
    fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
    const ModuleSource module_source) {
  auto module_path = parent_module_path.Clone();
  module_path.push_back(module_name);
  new StartModuleCall(
      &operation_queue_, this, module_path, module_url, link_name,
      manifest.Clone(), std::move(create_chain_info), module_source,
      nullptr /* surface_relation */, std::move(incoming_services),
      std::move(module_controller_request), nullptr /* embed_module_watcher */,
      std::move(view_owner_request), nullptr /* daisy */, [] {});
}

void StoryControllerImpl::StartModuleInShellDeprecated(
    const f1dl::VectorPtr<f1dl::StringPtr>& parent_module_path,
    const f1dl::StringPtr& module_name, const f1dl::StringPtr& module_url,
    const f1dl::StringPtr& link_name, const modular::ModuleManifestPtr manifest,
    CreateChainInfoPtr create_chain_info,
    f1dl::InterfaceRequest<component::ServiceProvider> incoming_services,
    f1dl::InterfaceRequest<ModuleController> module_controller_request,
    SurfaceRelationPtr surface_relation, const bool focus,
    ModuleSource module_source) {
  auto module_path = parent_module_path.Clone();
  module_path.push_back(module_name);
  new StartModuleInShellCall(
      &operation_queue_, this, module_path, module_url, link_name,
      manifest.Clone(), std::move(create_chain_info),
      std::move(incoming_services), std::move(module_controller_request),
      std::move(surface_relation), focus, module_source, nullptr /* daisy */,
      [] {});
}

void StoryControllerImpl::EmbedModule(
    const f1dl::VectorPtr<f1dl::StringPtr>& parent_module_path,
    const f1dl::StringPtr& module_name, DaisyPtr daisy,
    f1dl::InterfaceRequest<component::ServiceProvider> incoming_services,
    f1dl::InterfaceRequest<ModuleController> module_controller_request,
    fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
    ModuleSource module_source,
    std::function<void(StartModuleStatus)> callback) {
  new AddDaisyCall(&operation_queue_, this, parent_module_path.Clone(),
                   module_name, std::move(daisy), std::move(incoming_services),
                   std::move(module_controller_request),
                   nullptr /* surface_relation */,
                   std::move(view_owner_request), std::move(module_source),
                   std::move(callback));
}

void StoryControllerImpl::StartModule(
    const f1dl::VectorPtr<f1dl::StringPtr>& parent_module_path,
    const f1dl::StringPtr& module_name, DaisyPtr daisy,
    f1dl::InterfaceRequest<component::ServiceProvider> incoming_services,
    f1dl::InterfaceRequest<ModuleController> module_controller_request,
    SurfaceRelationPtr surface_relation, ModuleSource module_source,
    std::function<void(StartModuleStatus)> callback) {
  new AddDaisyCall(&operation_queue_, this, parent_module_path.Clone(),
                   module_name, std::move(daisy), std::move(incoming_services),
                   std::move(module_controller_request),
                   std::move(surface_relation),
                   nullptr /* view_owner_request */, std::move(module_source),
                   std::move(callback));
}

void StoryControllerImpl::StartContainerInShell(
    const f1dl::VectorPtr<f1dl::StringPtr>& parent_module_path,
    const f1dl::StringPtr& name,
    SurfaceRelationPtr parent_relation,
    f1dl::VectorPtr<ContainerLayoutPtr> layout,
    f1dl::VectorPtr<ContainerRelationEntryPtr> relationships,
    f1dl::VectorPtr<ContainerNodePtr> nodes) {
  new StartContainerInShellCall(
      &operation_queue_, this, parent_module_path.Clone(), name,
      std::move(parent_relation), std::move(layout), std::move(relationships),
      std::move(nodes));
}

void StoryControllerImpl::EmbedModuleDeprecated(
    const f1dl::VectorPtr<f1dl::StringPtr>& parent_module_path,
    const f1dl::StringPtr& module_name, const f1dl::StringPtr& module_url,
    const f1dl::StringPtr& link_name, CreateChainInfoPtr create_chain_info,
    f1dl::InterfaceRequest<component::ServiceProvider> incoming_services,
    f1dl::InterfaceRequest<ModuleController> module_controller_request,
    f1dl::InterfaceHandle<EmbedModuleWatcher> embed_module_watcher,
    fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request) {
  f1dl::VectorPtr<f1dl::StringPtr> module_path = parent_module_path.Clone();
  module_path.push_back(module_name);
  new StartModuleCall(
      &operation_queue_, this, module_path, module_url, link_name,
      nullptr /* module_manifest */, std::move(create_chain_info),
      ModuleSource::INTERNAL, nullptr /* surface_relation */,
      std::move(incoming_services), std::move(module_controller_request),
      std::move(embed_module_watcher), std::move(view_owner_request),
      nullptr /* daisy */, [] {});
}

void StoryControllerImpl::ProcessPendingViews() {
  // NOTE(mesch): As it stands, this machinery to send modules in traversal
  // order to the story shell is N^3 over the lifetime of the story, where N is
  // the number of modules. This function is N^2, and it's called once for each
  // of the N modules. However, N is small, and moreover its scale is limited my
  // much more severe constraints. Eventually, we will address this by changing
  // story shell to be able to accomodate modules out of traversal order.
  if (!story_shell_) {
    return;
  }

  std::vector<f1dl::StringPtr> added_keys;

  for (auto& kv : pending_views_) {
    auto* const connection = FindConnection(kv.second.first);
    if (!connection) {
      continue;
    }

    auto* const anchor = FindAnchor(connection);
    if (!anchor) {
      continue;
    }

    const auto anchor_view_id = PathString(anchor->module_data->module_path);
    if (!connected_views_.count(anchor_view_id)) {
      continue;
    }

    const auto view_id = PathString(kv.second.first);
    story_shell_->ConnectView(std::move(kv.second.second), view_id,
                              anchor_view_id,
                              connection->module_data->surface_relation.Clone(),
                              nullptr /* module_manifest */);
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

void StoryControllerImpl::OnPageChange(const std::string& key,
                                       const std::string& value) {
  auto module_data = ModuleData::New();
  if (!XdrRead(value, &module_data, XdrModuleData)) {
    FXL_LOG(ERROR) << "Unable to parse ModuleData " << key << " " << value;
    return;
  }

  // Check if we already have a blocked operation for this update.
  auto i = std::find_if(
      blocked_operations_.begin(), blocked_operations_.end(),
      [&module_data](const auto& p) { return p.first.Equals(module_data); });
  if (i != blocked_operations_.end()) {
    // For an already blocked operation, we simply continue the operation.
    auto op = i->second;
    blocked_operations_.erase(i);
    op->Continue();
    return;
  }

  // Control reaching here means that this update came from a remote device.
  new LedgerNotificationCall(&operation_queue_, this, std::move(module_data));
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
void StoryControllerImpl::SetInfoExtra(const f1dl::StringPtr& name,
                                       const f1dl::StringPtr& value,
                                       const SetInfoExtraCallback& callback) {
  story_provider_impl_->SetStoryInfoExtra(story_id_, name, value, callback);
}

// |StoryController|
void StoryControllerImpl::Start(
    fidl::InterfaceRequest<views_v1_token::ViewOwner> request) {
  new StartCall(&operation_queue_, this, std::move(request));
}

// |StoryController|
void StoryControllerImpl::Stop(const StopCallback& done) {
  new StopCall(&operation_queue_, this, true /* notify */, done);
}

// |StoryController|
void StoryControllerImpl::Watch(f1dl::InterfaceHandle<StoryWatcher> watcher) {
  auto ptr = watcher.Bind();
  ptr->OnStateChange(state_);
  watchers_.AddInterfacePtr(std::move(ptr));
}

// |StoryController|
void StoryControllerImpl::AddModuleDeprecated(
    f1dl::VectorPtr<f1dl::StringPtr> module_path,
    const f1dl::StringPtr& module_name,
    const f1dl::StringPtr& module_url,
    const f1dl::StringPtr& link_name,
    SurfaceRelationPtr surface_relation) {
  // In the API, a null module path is allowed to represent the empty module
  // path.
  if (module_path.is_null()) {
    module_path.resize(0);
  }

  module_path.push_back(module_name);

  new AddModuleCall(&operation_queue_, this, std::move(module_path), module_url,
                    link_name, std::move(surface_relation), [] {});
}

// |StoryController|
void StoryControllerImpl::GetActiveModules(
    f1dl::InterfaceHandle<StoryModulesWatcher> watcher,
    const GetActiveModulesCallback& callback) {
  // We execute this in a SyncCall so that we are sure we don't fall in a crack
  // between a module being created and inserted in the connections collection
  // during some Operation.
  new SyncCall(&operation_queue_,
               fxl::MakeCopyable(
                   [ this, watcher = std::move(watcher), callback ]() mutable {
                     if (watcher) {
                       auto ptr = watcher.Bind();
                       modules_watchers_.AddInterfacePtr(std::move(ptr));
                     }

                     f1dl::VectorPtr<ModuleDataPtr> result;
                     result.resize(0);
                     for (auto& connection : connections_) {
                       result.push_back(connection.module_data.Clone());
                     }
                     callback(std::move(result));
                   }));
}

// |StoryController|
void StoryControllerImpl::GetModules(const GetModulesCallback& callback) {
  new ReadAllDataCall<ModuleData>(&operation_queue_, page(), kModuleKeyPrefix,
                                  XdrModuleData, callback);
}

// |StoryController|
void StoryControllerImpl::GetModuleController(
    f1dl::VectorPtr<f1dl::StringPtr> module_path,
    f1dl::InterfaceRequest<ModuleController> request) {
  new SyncCall(&operation_queue_, fxl::MakeCopyable([
    this, module_path = std::move(module_path), request = std::move(request)
  ]() mutable {
    for (auto& connection : connections_) {
      if (module_path.Equals(connection.module_data->module_path)) {
        connection.module_controller_impl->Connect(std::move(request));
        return;
      }
    }

    // Trying to get a controller for a module that is not active just
    // drops the connection request.
  }));
}

// |StoryController|
void StoryControllerImpl::GetActiveLinks(
    f1dl::InterfaceHandle<StoryLinksWatcher> watcher,
    const GetActiveLinksCallback& callback) {
  // We execute this in a SyncCall so that we are sure we don't fall in a crack
  // between a link being created and inserted in the links collection during
  // some Operation. (Right now Links are not created in an Operation, but we
  // don't want to rely on it.)
  new SyncCall(&operation_queue_,
               fxl::MakeCopyable(
                   [ this, watcher = std::move(watcher), callback ]() mutable {
                     if (watcher) {
                       auto ptr = watcher.Bind();
                       links_watchers_.AddInterfacePtr(std::move(ptr));
                     }

                     // Only active links, i.e. links currently in use by a
                     // module, are returned here. Eventually we might want to
                     // list all links, but this requires some changes to how
                     // links are stored to make it nice. (Right now we need to
                     // parse keys, which we don't want to.)
                     f1dl::VectorPtr<LinkPathPtr> result;
                     result.resize(0);
                     for (auto& link : links_) {
                       result.push_back(link->link_path().Clone());
                     }
                     callback(std::move(result));
                   }));
}

// |StoryController|
void StoryControllerImpl::GetLink(f1dl::VectorPtr<f1dl::StringPtr> module_path,
                                  const f1dl::StringPtr& name,
                                  f1dl::InterfaceRequest<Link> request) {
  // In the API, a null module path is allowed to represent the empty module
  // path.
  if (module_path.is_null()) {
    module_path.resize(0);
  }

  LinkPathPtr link_path = LinkPath::New();
  link_path->module_path = std::move(module_path);
  link_path->link_name = name;
  ConnectLinkPath(std::move(link_path), LinkImpl::ConnectionType::Secondary,
                  std::move(request));
}

void StoryControllerImpl::AddModule(
    f1dl::VectorPtr<f1dl::StringPtr> parent_module_path,
    const f1dl::StringPtr& module_name,
    DaisyPtr daisy,
    SurfaceRelationPtr surface_relation) {
  new AddDaisyCall(
      &operation_queue_, this, std::move(parent_module_path), module_name,
      std::move(daisy), nullptr /* incoming_services */,
      nullptr /* module_controller_request */, std::move(surface_relation),
      nullptr /* view_owner_request */, ModuleSource::EXTERNAL,
      [](StartModuleStatus) {});
}

void StoryControllerImpl::StartStoryShell(
    fidl::InterfaceRequest<views_v1_token::ViewOwner> request) {
  story_shell_app_ = story_provider_impl_->StartStoryShell(std::move(request));
  story_shell_app_->services().ConnectToService(story_shell_.NewRequest());
  story_shell_->Initialize(story_context_binding_.NewBinding());
}

void StoryControllerImpl::NotifyStateChange() {
  watchers_.ForAllPtrs(
      [this](StoryWatcher* const watcher) { watcher->OnStateChange(state_); });

  story_provider_impl_->NotifyStoryStateChange(story_id_, state_);

  // NOTE(mesch): This gets scheduled on the StoryControllerImpl Operation
  // queue. If the current StoryControllerImpl Operation is part of a
  // DeleteStory Operation of the StoryProviderImpl, then the SetStoryState
  // Operation gets scheduled after the delete of the story is completed, and it
  // will not execute because its queue is deleted beforehand.
  //
  // TODO(mesch): Maybe we should execute this inside the containing Operation.

  PerDeviceStoryInfoPtr data = PerDeviceStoryInfo::New();
  data->device_id = story_provider_impl_->device_id();
  data->story_id = story_id_;
  data->timestamp = time(nullptr);
  data->state = state_;

  new WriteDataCall<PerDeviceStoryInfo, PerDeviceStoryInfoPtr>(
      &operation_queue_, page(), MakePerDeviceKey(data->device_id),
      XdrPerDeviceStoryInfo, std::move(data), [] {});
}

void StoryControllerImpl::DisposeLink(LinkImpl* const link) {
  auto f = std::find_if(
      links_.begin(), links_.end(),
      [link](const std::unique_ptr<LinkImpl>& l) { return l.get() == link; });
  FXL_DCHECK(f != links_.end());
  links_.erase(f);
}

bool StoryControllerImpl::IsExternalModule(
    const f1dl::VectorPtr<f1dl::StringPtr>& module_path) {
  auto* const i = FindConnection(module_path);
  if (!i) {
    return false;
  }

  return i->module_data->module_source == ModuleSource::EXTERNAL;
}

void StoryControllerImpl::OnModuleStateChange(
    const f1dl::VectorPtr<f1dl::StringPtr>& module_path,
    const ModuleState state) {
  if (!track_root_module_state_) {
    return;
  }

  if (first_module_path_.is_null()) {
    first_module_path_ = module_path.Clone();
  }
  if (first_module_path_.Equals(module_path)) {
    UpdateStoryState(state);
  }

  if (IsExternalModule(module_path) && state == ModuleState::DONE) {
    StopModule(module_path, [] {});
  }
}

void StoryControllerImpl::UpdateStoryState(const ModuleState state) {
  switch (state) {
    case ModuleState::STARTING:
      state_ = StoryState::STARTING;
      break;
    case ModuleState::RUNNING:
    case ModuleState::UNLINKED:
      state_ = StoryState::RUNNING;
      break;
    case ModuleState::STOPPED:
      // TODO(mesch): The story should only be marked STOPPED after
      // StoryContoller.Stop() is executed, and no modules are left running. In
      // this state here, there may be modules other than the root module left
      // running. These modules may even request more modules to start or make
      // suggestions to start more modules, which would be shown to the
      // user. However, the calls to run the modules would silently not result
      // in modules running, just in the modules to be added to the story
      // record, because actually starting newly added modules is gated by the
      // story to be running. This makes little sense. FW-334
      state_ = StoryState::STOPPED;
      break;
    case ModuleState::DONE:
      // TODO(mesch): Same problem for modules remaining running and for newly
      // added modules as for STOPPED. FW-334
      state_ = StoryState::DONE;
      break;
    case ModuleState::ERROR:
      state_ = StoryState::ERROR;
      break;
  }

  NotifyStateChange();
}

StoryControllerImpl::Connection* StoryControllerImpl::FindConnection(
    const f1dl::VectorPtr<f1dl::StringPtr>& module_path) {
  f1dl::StringPtr path;
  if (module_path->size() >= 1) {
    f1dl::StringPtr path = module_path->at(module_path->size() - 1);
  }
  for (auto& c : connections_) {
    if (c.module_data->module_path.Equals(module_path)) {
      return &c;
    }
  }
  return nullptr;
}

StoryControllerImpl::Connection* StoryControllerImpl::FindAnchor(
    Connection* connection) {
  if (!connection) {
    return nullptr;
  }

  auto* anchor =
      FindConnection(ParentModulePath(connection->module_data->module_path));

  // Traverse up until there is a non-embedded module. We recognize non-embedded
  // modules by having a non-null SurfaceRelation. If the root module is there
  // at all, it has a non-null surface relation.
  while (anchor && anchor->module_data->surface_relation.is_null()) {
    anchor = FindConnection(ParentModulePath(anchor->module_data->module_path));
  }

  return anchor;
}

StoryControllerImpl::Connection* StoryControllerImpl::FindEmbedder(
    const f1dl::VectorPtr<f1dl::StringPtr>& module_path) {
  // Traverse up until there is an embedder module. We recognize embedder
  // modules by having a non-null EmbedModuleWatcher.
  auto* parent = FindConnection(ParentModulePath(module_path));
  while (parent) {
    if (parent->embed_module_watcher) {
      return parent;
    }
    parent = FindConnection(ParentModulePath(parent->module_data->module_path));
  }

  return nullptr;
}

}  // namespace modular
