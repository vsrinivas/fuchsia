// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/link_impl.h"

#include <functional>

#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/src/story_runner/incremental_link.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"
#include "apps/modular/lib/util/debug.h"

namespace modular {

constexpr bool kEnableIncrementalLinks{true};

class LinkImpl::ReadCall : Operation<> {
 public:
  ReadCall(OperationContainer* const container,
           LinkImpl* const impl,
           ResultCall result_call)
      : Operation("LinkImpl::ReadCall", container, std::move(result_call)),
        impl_(impl) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    impl_->link_storage_->ReadLinkData(impl_->link_path_,
                                       [this, flow](const fidl::String& json) {
                                         if (!json.is_null()) {
                                           impl_->doc_.Parse(json.get());
                                         }
                                       });
  }

  LinkImpl* const impl_;  // not owned

  FTL_DISALLOW_COPY_AND_ASSIGN(ReadCall);
};

class LinkImpl::WriteCall : Operation<> {
 public:
  WriteCall(OperationContainer* const container,
            LinkImpl* const impl,
            const uint32_t src,
            ResultCall result_call)
      : Operation("LinkImpl::WriteCall", container, std::move(result_call)),
        impl_(impl),
        src_(src) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    impl_->link_storage_->WriteLinkData(impl_->link_path_,
                                        JsonValueToString(impl_->doc_),
                                        [this, flow] { Cont1(flow); });
  }

  void Cont1(FlowToken flow) {
    impl_->link_storage_->FlushWatchers([this, flow] { Cont2(flow); });
  }

  void Cont2(FlowToken /*flow*/) {
    impl_->NotifyWatchers(src_);
  }

  LinkImpl* const impl_;  // not owned
  const uint32_t src_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteCall);
};

class LinkImpl::SetSchemaCall : Operation<> {
 public:
  SetSchemaCall(OperationContainer* const container,
                LinkImpl* const impl,
                const fidl::String& json_schema)
      : Operation("LinkImpl::SetSchemaCall", container, [] {}),
        impl_(impl),
        json_schema_(json_schema) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    rapidjson::Document doc;
    doc.Parse(json_schema_.get());
    if (doc.HasParseError()) {
      FTL_LOG(ERROR) << "LinkImpl::SetSchema() "
                     << EncodeLinkPath(impl_->link_path_)
                     << " JSON parse failed error #" << doc.GetParseError()
                     << std::endl
                     << json_schema_;
      return;
    }

    impl_->schema_doc_ = std::make_unique<rapidjson::SchemaDocument>(doc);
  }

  LinkImpl* const impl_;  // not owned
  const fidl::String json_schema_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SetSchemaCall);
};

class LinkImpl::GetCall : Operation<fidl::String> {
 public:
  GetCall(OperationContainer* const container,
          LinkImpl* const impl,
          fidl::Array<fidl::String> path,
          ResultCall result_call)
      : Operation("LinkImpl::GetCall", container, std::move(result_call)),
        impl_(impl),
        path_(std::move(path)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &result_};

    auto p = CreatePointer(impl_->doc_, path_).Get(impl_->doc_);

    if (p != nullptr) {
      result_ = fidl::String(JsonValueToString(*p));
    }
  }

  LinkImpl* const impl_;  // not owned
  const fidl::Array<fidl::String> path_;
  fidl::String result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetCall);
};

class LinkImpl::SetCall : Operation<> {
 public:
  SetCall(OperationContainer* const container,
          LinkImpl* const impl,
          fidl::Array<fidl::String> path,
          const fidl::String& json,
          const uint32_t src)
      : Operation("LinkImpl::SetCall", container, [] {}),
        impl_(impl),
        path_(std::move(path)),
        json_(json),
        src_(src) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    CrtJsonPointer ptr = CreatePointer(impl_->doc_, path_);
    const bool success = impl_->ApplySetOp(ptr, json_);
    if (success) {
      impl_->ValidateSchema("LinkImpl::SetCall", ptr, json_);
      new WriteCall(&operation_queue_, impl_, src_, [flow] {});
      impl_->NotifyWatchers(src_);
    } else {
      FTL_LOG(WARNING) << "LinkImpl::SetCall failed " << json_;
    }
  }

  LinkImpl* const impl_;  // not owned
  const fidl::Array<fidl::String> path_;
  const fidl::String json_;
  const uint32_t src_;

  // WriteCall is executed here.
  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SetCall);
};

class LinkImpl::UpdateObjectCall : Operation<> {
 public:
  UpdateObjectCall(OperationContainer* const container,
                   LinkImpl* const impl,
                   fidl::Array<fidl::String> path,
                   const fidl::String& json,
                   const uint32_t src)
      : Operation("LinkImpl::UpdateObjectCall", container, [] {}),
        impl_(impl),
        path_(std::move(path)),
        json_(json),
        src_(src) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    CrtJsonPointer ptr = CreatePointer(impl_->doc_, path_);
    const bool success = impl_->ApplyUpdateOp(ptr, json_);
    if (success) {
      impl_->ValidateSchema("LinkImpl::UpdateObject", ptr, json_);
      new WriteCall(&operation_queue_, impl_, src_, [flow] {});
      impl_->NotifyWatchers(src_);
    } else {
      FTL_LOG(WARNING) << "LinkImpl::UpdateObjectCall failed " << json_;
    }
  }

  LinkImpl* const impl_;  // not owned
  const fidl::Array<fidl::String> path_;
  const fidl::String json_;
  const uint32_t src_;

  // WriteCall is executed here.
  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UpdateObjectCall);
};

class LinkImpl::EraseCall : Operation<> {
 public:
  EraseCall(OperationContainer* const container,
            LinkImpl* const impl,
            fidl::Array<fidl::String> path,
            const uint32_t src)
      : Operation("LinkImpl::EraseCall", container, [] {}),
        impl_(impl),
        path_(std::move(path)),
        src_(src) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    CrtJsonPointer ptr = CreatePointer(impl_->doc_, path_);
    const bool success = impl_->ApplyEraseOp(ptr);
    if (success) {
      impl_->ValidateSchema("LinkImpl::EraseCall", ptr, std::string());
      new WriteCall(&operation_queue_, impl_, src_, [flow] {});
      impl_->NotifyWatchers(src_);
    } else {
      FTL_LOG(WARNING) << "LinkImpl::EraseCall failed ";
    }
  }

  LinkImpl* const impl_;  // not owned
  const fidl::Array<fidl::String> path_;
  const uint32_t src_;

  // WriteCall is executed here.
  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(EraseCall);
};

class LinkImpl::WatchCall : Operation<> {
 public:
  WatchCall(OperationContainer* const container,
            LinkImpl* const impl,
            fidl::InterfaceHandle<LinkWatcher> watcher,
            const uint32_t conn)
      : Operation("LinkImpl::WatchCall", container, [] {}),
        impl_(impl),
        watcher_(LinkWatcherPtr::Create(std::move(watcher))),
        conn_(conn) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    // TODO(jimbe): We need to send an initial notification of state until there
    // is snapshot information that can be used by clients to query the state at
    // this instant. Otherwise there is no sequence information about total
    // state versus incremental changes.
    //
    // TODO(mesch): We should adopt the pattern from ledger to read the value
    // and register a watcher for subsequent changes in the same operation, so
    // that we don't have to send the current value to the watcher.
    watcher_->Notify(JsonValueToString(impl_->doc_));

    impl_->watchers_.emplace_back(std::make_unique<LinkWatcherConnection>(
        impl_, std::move(watcher_), conn_));
  }

  LinkImpl* const impl_;  // not owned
  LinkWatcherPtr watcher_;
  const uint32_t conn_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WatchCall);
};

class LinkImpl::ChangeCall : Operation<> {
 public:
  ChangeCall(OperationContainer* const container,
             LinkImpl* const impl,
             const fidl::String& json)
      : Operation("LinkImpl::ChangeCall", container, [] {}),
        impl_(impl),
        json_(json) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    // NOTE(jimbe) With rapidjson, the opposite check is more expensive, O(n^2),
    // so we won't do it for now. See case kObjectType in operator==() in
    // include/rapidjson/document.h.
    //
    //  if (doc_.Equals(json)) {
    //    return;
    //  }
    //
    // Since all json in a link was written by the same serializer, this check
    // is mostly accurate. This test has false negatives when only order
    // differs.
    if (json_ == JsonValueToString(impl_->doc_)) {
      return;
    }

    impl_->doc_.Parse(json_);
    impl_->NotifyWatchers(kOnChangeConnectionId);
  }

  LinkImpl* const impl_;  // not owned
  const fidl::String json_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ChangeCall);
};

LinkImpl::LinkImpl(LinkStorage* const link_storage, LinkPathPtr link_path)
    : link_path_(std::move(link_path)), link_storage_(link_storage) {
  new ReloadCall(&operation_queue_, this, [this] {
    for (auto& request : requests_) {
      LinkConnection::New(this, next_connection_id_++, std::move(request));
    }
    requests_.clear();
    ready_ = true;
  });

  link_storage_->WatchLink(
      link_path_, this, [this](const fidl::String& json) { OnChange(json); });
}

LinkImpl::~LinkImpl() {
  link_storage_->DropWatcher(this);
}

void LinkImpl::Connect(fidl::InterfaceRequest<Link> request) {
  if (ready_) {
    LinkConnection::New(this, next_connection_id_++, std::move(request));
  } else {
    requests_.emplace_back(std::move(request));
  }
}

void LinkImpl::SetSchema(const fidl::String& json_schema) {
  // TODO(jimbe, mesch): This method needs a success status,
  // otherwise clients have no way to know they sent bogus data.
  new SetSchemaCall(&operation_queue_, this, json_schema);
}

void LinkImpl::Get(fidl::Array<fidl::String> path,
                   const std::function<void(fidl::String)>& callback) {
  new GetCall(&operation_queue_, this, std::move(path), callback);
}

// The |src| argument identifies which client made the call to Set() or
// Update(), so that it notifies either all clients or all other clients,
// depending on whether WatchAll() or Watch() was called, respectively.
//
// When a watcher is registered, it first receives an OnChange() call with the
// current value. Thus, when a client first calls Set() and then Watch(), its
// LinkWatcher receives the value that was just Set(). This should not be
// surprising, and clients should register their watchers first before setting
// the link value.
void LinkImpl::Set(fidl::Array<fidl::String> path,
                   const fidl::String& json,
                   const uint32_t src) {
  // TODO(jimbe, mesch): This method needs a success status, otherwise clients
  // have no way to know they sent bogus data.

  if (kEnableIncrementalLinks) {
    LinkChangePtr data = LinkChange::New();
    // Leave data->key null to signify a new entry
    data->op = LinkChangeOp::SET;
    data->pointer = std::move(path);
    data->json = json;
    new IncrementalChangeCall(&operation_queue_, this, std::move(data), src);
  } else {
    new SetCall(&operation_queue_, this, std::move(path), json, src);
  }
}

void LinkImpl::UpdateObject(fidl::Array<fidl::String> path,
                            const fidl::String& json, const uint32_t src) {
  // TODO(jimbe, mesch): This method needs a success status,
  // otherwise clients have no way to know they sent bogus data.

  if (kEnableIncrementalLinks) {
    LinkChangePtr data = LinkChange::New();
    // Leave data->key empty to signify a new entry
    data->op = LinkChangeOp::UPDATE;
    data->pointer = std::move(path);
    data->json = json;
    new IncrementalChangeCall(&operation_queue_, this, std::move(data), src);
  } else {
    new UpdateObjectCall(&operation_queue_, this, std::move(path), json, src);
  }
}

void LinkImpl::Erase(fidl::Array<fidl::String> path, const uint32_t src) {
  if (kEnableIncrementalLinks) {
    LinkChangePtr data = LinkChange::New();
    // Leave data->key empty to signify a new entry
    data->op = LinkChangeOp::ERASE;
    data->pointer = std::move(path);
    // Leave data->json null for ERASE.

    new IncrementalChangeCall(&operation_queue_, this, std::move(data), src);
  } else {
    new EraseCall(&operation_queue_, this, std::move(path), src);
  }

}

void LinkImpl::Sync(const std::function<void()>& callback) {
  new SyncCall(&operation_queue_, callback);
}

bool LinkImpl::ApplySetOp(const CrtJsonPointer& ptr, const fidl::String& json) {
  CrtJsonDoc new_value;
  new_value.Parse(json);
  if (new_value.HasParseError()) {
    FTL_LOG(ERROR) << "LinkImpl::ApplySetOp() " << EncodeLinkPath(link_path_)
                   << " JSON parse failed error #" << new_value.GetParseError()
                   << std::endl
                   << json;
    return false;
  }

  ptr.Set(doc_, std::move(new_value));
  return true;
}

bool LinkImpl::ApplyUpdateOp(const CrtJsonPointer& ptr,
                             const fidl::String& json) {
  CrtJsonDoc new_value;
  new_value.Parse(json);
  if (new_value.HasParseError()) {
    FTL_LOG(ERROR) << "LinkImpl::ApplyUpdateOp() " << EncodeLinkPath(link_path_)
                   << " JSON parse failed error #" << new_value.GetParseError()
                   << std::endl
                   << json;
    return false;
  }

  CrtJsonValue& current_value = ptr.Create(doc_);
  MergeObject(current_value, std::move(new_value), doc_.GetAllocator());
  return true;
}

bool LinkImpl::ApplyEraseOp(const CrtJsonPointer& ptr) {
  return ptr.Erase(doc_);
}

// Merges source into target. The values will be move()'d out of |source|.
// Returns true if the merge operation caused any changes.
bool LinkImpl::MergeObject(CrtJsonValue& target,
                           CrtJsonValue&& source,
                           CrtJsonValue::AllocatorType& allocator) {
  if (!source.IsObject()) {
    FTL_LOG(WARNING) << "LinkImpl::MergeObject() - source is not an object "
                  << JsonValueToPrettyString(source);
    return false;
  }

  if (!target.IsObject()) {
    target = std::move(source);
    return true;
  }

  bool diff = false;
  for (auto& source_itr : source.GetObject()) {
    auto target_itr = target.FindMember(source_itr.name);
    // If the value already exists and not identical, set it.
    if (target_itr == target.MemberEnd()) {
      target.AddMember(std::move(source_itr.name), std::move(source_itr.value),
                       allocator);
      diff = true;
    } else if (source_itr.value != target_itr->value) {
      // TODO(jimbe) The above comparison is O(n^2). Need to revisit the
      // detection logic.
      target_itr->value = std::move(source_itr.value);
      diff = true;
    }
  }
  return diff;
}

void LinkImpl::ValidateSchema(const char* const entry_point,
                              const CrtJsonPointer& debug_pointer,
                              const std::string& debug_json) {
  if (!schema_doc_) {
    return;
  }

  rapidjson::GenericSchemaValidator<rapidjson::SchemaDocument> validator(
      *schema_doc_);
  if (!doc_.Accept(validator)) {
    if (!validator.IsValid()) {
      rapidjson::StringBuffer sbpath;
      validator.GetInvalidSchemaPointer().StringifyUriFragment(sbpath);
      rapidjson::StringBuffer sbdoc;
      validator.GetInvalidDocumentPointer().StringifyUriFragment(sbdoc);
      rapidjson::StringBuffer sbapipath;
      debug_pointer.StringifyUriFragment(sbapipath);
      FTL_LOG(ERROR) << "Schema constraint violation in "
                     << EncodeLinkPath(link_path_) << ":" << std::endl
                     << "  Constraint " << sbpath.GetString() << "/"
                     << validator.GetInvalidSchemaKeyword() << std::endl
                     << "  Doc location: " << sbdoc.GetString() << std::endl
                     << "  API " << entry_point << std::endl
                     << "  API path " << sbapipath.GetString() << std::endl
                     << "  API json " << debug_json << std::endl;
    }
  }
}

void LinkImpl::OnChange(const fidl::String& json) {
  LinkChangePtr data;
  if (!XdrRead(json, &data, XdrLinkChange)) {
    FTL_LOG(ERROR) << EncodeLinkPath(link_path_)
                   << "LinkImpl::OnChange() - XdrRead failed!";
    return;
  }

  new IncrementalChangeCall(&operation_queue_, this, std::move(data),
                            kOnChangeConnectionId);
}

// To be called after:
// - API call for Set/Update/Erase. Happens at Operation execution, not
//   after PageChange event is received from the Ledger.
// - Change is received from another device in OnChange().
void LinkImpl::NotifyWatchers(const uint32_t src) {
  const fidl::String value = JsonValueToString(doc_);
  for (auto& dst : watchers_) {
    dst->Notify(value, src);
  }
}

void LinkImpl::AddConnection(LinkConnection* const connection) {
  connections_.emplace_back(connection);
}

void LinkImpl::RemoveConnection(LinkConnection* const connection) {
  auto it =
      std::remove_if(connections_.begin(), connections_.end(),
                     [connection](const std::unique_ptr<LinkConnection>& p) {
                       return p.get() == connection;
                     });
  FTL_DCHECK(it != connections_.end());
  connections_.erase(it, connections_.end());

  // The link must be fully synced before we can call the orphaned handler
  // because the write storage call calls back onto this. Also, we must check
  // whether it's still orphaned again after Sync, because a once orphaned link
  // can acquire new connections because it can be connected to by name. This
  // requires that the orphaned handler executes synchronously.
  //
  // TODO(mesch): This is still not correct as it leaves the possibility that
  // another set operation was executed after Sync().
  if (connections_.empty() && orphaned_handler_) {
    Sync([this] {
      if (connections_.empty() && orphaned_handler_) {
        orphaned_handler_();
      }
    });
  }
}

void LinkImpl::RemoveConnection(LinkWatcherConnection* const connection) {
  auto i = std::remove_if(
      watchers_.begin(), watchers_.end(),
      [connection](const std::unique_ptr<LinkWatcherConnection>& p) {
        return p.get() == connection;
      });
  FTL_DCHECK(i != watchers_.end());
  watchers_.erase(i, watchers_.end());
}

void LinkImpl::Watch(fidl::InterfaceHandle<LinkWatcher> watcher,
                     const uint32_t conn) {
  new WatchCall(&operation_queue_, this, std::move(watcher), conn);
}

void LinkImpl::WatchAll(fidl::InterfaceHandle<LinkWatcher> watcher) {
  Watch(std::move(watcher), kWatchAllConnectionId);
}

LinkConnection::LinkConnection(LinkImpl* const impl,
                               const uint32_t id,
                               fidl::InterfaceRequest<Link> link_request)
    : impl_(impl), binding_(this, std::move(link_request)), id_(id) {
  impl_->AddConnection(this);
  binding_.set_connection_error_handler(
      [this] { impl_->RemoveConnection(this); });
}

LinkConnection::~LinkConnection() = default;

void LinkConnection::Watch(fidl::InterfaceHandle<LinkWatcher> watcher) {
  // This watcher stays associated with the connection it was registered
  // through. The ID is used to block notifications for updates that originate
  // at the same connection.
  impl_->Watch(std::move(watcher), id_);
}

void LinkConnection::WatchAll(fidl::InterfaceHandle<LinkWatcher> watcher) {
  // This watcher is not associated with the connection it was registered
  // through. The connection is recorded as 0, which never identifies any
  // connection that originates an update, so no update notification is ever
  // blocked.
  impl_->WatchAll(std::move(watcher));
}

void LinkConnection::Sync(const SyncCallback& callback) {
  impl_->Sync(callback);
}

void LinkConnection::SetSchema(const fidl::String& json_schema) {
  impl_->SetSchema(json_schema);
}

void LinkConnection::UpdateObject(fidl::Array<fidl::String> path,
                                  const fidl::String& json) {
  impl_->UpdateObject(std::move(path), json, id_);
}

void LinkConnection::Set(fidl::Array<fidl::String> path,
                         const fidl::String& json) {
  impl_->Set(std::move(path), json, id_);
}

void LinkConnection::Erase(fidl::Array<fidl::String> path) {
  impl_->Erase(std::move(path), id_);
}

void LinkConnection::Get(fidl::Array<fidl::String> path,
                         const GetCallback& callback) {
  impl_->Get(std::move(path), callback);
}

LinkWatcherConnection::LinkWatcherConnection(LinkImpl* const impl,
                                             LinkWatcherPtr watcher,
                                             const uint32_t conn)
    : impl_(impl), watcher_(std::move(watcher)), conn_(conn) {
  watcher_.set_connection_error_handler(
      [this] { impl_->RemoveConnection(this); });
}

LinkWatcherConnection::~LinkWatcherConnection() = default;

void LinkWatcherConnection::Notify(const fidl::String& value,
                                   const uint32_t src) {
  if (conn_ != src) {
    watcher_->Notify(value);
  }
}

}  // namespace modular
