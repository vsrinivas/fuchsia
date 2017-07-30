// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/link_impl.h"

#include <functional>

#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/services/story/link.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"

namespace modular {

namespace {

template <typename Doc>
rapidjson::GenericPointer<typename Doc::ValueType> CreatePointerFromArray(
    const Doc& doc,
    typename fidl::Array<fidl::String>::Iterator begin,
    typename fidl::Array<fidl::String>::Iterator end) {
  rapidjson::GenericPointer<typename Doc::ValueType> pointer;
  for (auto it = begin; it != end; ++it) {
    pointer = pointer.Append(it->get(), nullptr);
  }
  return pointer;
}

}  // namespace

LinkImpl::LinkImpl(StoryStorageImpl* const story_storage,
                   const LinkPathPtr& link_path)
    : link_path_(link_path.Clone()),
      story_storage_(story_storage),
      write_link_data_(Bottleneck::FRONT, this, &LinkImpl::WriteLinkDataImpl) {
  ReadLinkData([this] {
    for (auto& request : requests_) {
      LinkConnection::New(this, std::move(request));
    }
    requests_.clear();
    ready_ = true;
  });

  story_storage_->WatchLink(
      link_path, this, [this](const fidl::String& json) { OnChange(json); });
}

LinkImpl::~LinkImpl() {
  story_storage_->DropWatcher(this);
}

void LinkImpl::Connect(fidl::InterfaceRequest<Link> request) {
  if (ready_) {
    LinkConnection::New(this, std::move(request));
  } else {
    requests_.emplace_back(std::move(request));
  }
}

void LinkImpl::SetSchema(const fidl::String& json_schema) {
  rapidjson::Document doc;
  doc.Parse(json_schema.get());
  if (doc.HasParseError()) {
    // TODO(jimbe, mesch): This method needs a success status,
    // otherwise clients have no way to know they sent bogus data.
    FTL_LOG(ERROR) << "LinkImpl::SetSchema() " << EncodeLinkPath(link_path_)
                   << " JSON parse failed error #" << doc.GetParseError()
                   << std::endl
                   << json_schema;
    return;
  }
  schema_doc_ = std::make_unique<rapidjson::SchemaDocument>(doc);
}

// The |LinkConnection| object knows which client made the call to Set() or
// Update(), so it notifies either all clients or all other clients, depending
// on whether WatchAll() or Watch() was called, respectively.
//
// When a watcher is registered, it first receives an OnChange() call with the
// current value. Thus, when a client first calls Set() and then Watch(), its
// LinkWatcher receives the value that was just Set(). This should not be
// surprising, and clients should register their watchers first before setting
// the link value. TODO(mesch): We should adopt the pattern from ledger to read
// the value and register a watcher for subsequent changes in the same
// operation, so that we don't have to send the current value to the watcher.
void LinkImpl::Set(fidl::Array<fidl::String> path,
                   const fidl::String& json,
                   LinkConnection* const src) {
  CrtJsonDoc new_value;
  new_value.Parse(json);
  if (new_value.HasParseError()) {
    // TODO(jimbe, mesch): This method needs a success status,
    // otherwise clients have no way to know they sent bogus data.
    FTL_LOG(ERROR) << "LinkImpl::Set() " << EncodeLinkPath(link_path_)
                   << " JSON parse failed error #" << new_value.GetParseError()
                   << std::endl
                   << json.get();
    return;
  }

  bool dirty = true;
  bool alreadyExist = false;

  CrtJsonPointer ptr = CreatePointerFromArray(doc_, path.begin(), path.end());
  CrtJsonValue& current_value =
      ptr.Create(doc_, doc_.GetAllocator(), &alreadyExist);
  if (alreadyExist) {
    dirty = new_value != current_value;
  }

  if (dirty) {
    ptr.Set(doc_, new_value);
    ValidateSchema("LinkImpl::Set", ptr, json.get());
    DatabaseChanged(src);
  }
}

void LinkImpl::Get(fidl::Array<fidl::String> path,
                   const std::function<void(fidl::String)>& callback) {
  auto p = CreatePointerFromArray(doc_, path.begin(), path.end()).Get(doc_);
  if (p == nullptr) {
    callback(fidl::String());
  } else {
    callback(fidl::String(JsonValueToString(*p)));
  }
}

void LinkImpl::UpdateObject(fidl::Array<fidl::String> path,
                            const fidl::String& json,
                            LinkConnection* const src) {
  CrtJsonDoc new_value;
  new_value.Parse(json);
  if (new_value.HasParseError()) {
    // TODO(jimbe, mesch): This method needs a success status,
    // otherwise clients have no way to know they sent bogus data.
    FTL_LOG(ERROR) << "LinkImpl::UpdateObject() " << EncodeLinkPath(link_path_)
                   << " JSON parse failed error #" << new_value.GetParseError()
                   << std::endl
                   << json.get();
    return;
  }

  auto ptr = CreatePointerFromArray(doc_, path.begin(), path.end());
  CrtJsonValue& current_value = ptr.Create(doc_);

  const bool dirty =
      MergeObject(current_value, std::move(new_value), doc_.GetAllocator());
  if (dirty) {
    ValidateSchema("LinkImpl::UpdateObject", ptr, json.get());
    DatabaseChanged(src);
  }
}

void LinkImpl::Erase(fidl::Array<fidl::String> path,
                     LinkConnection* const src) {
  auto ptr = CreatePointerFromArray(doc_, path.begin(), path.end());
  auto value = ptr.Get(doc_);
  if (value != nullptr && ptr.Erase(doc_)) {
    ValidateSchema("LinkImpl::Erase", ptr, std::string());
    DatabaseChanged(src);
  }
}

void LinkImpl::Sync(const std::function<void()>& callback) {
  story_storage_->Sync(callback);
}

// Merges source into target. The values will be move()'d out of |source|.
// Returns true if the merge operation caused any changes.
bool LinkImpl::MergeObject(CrtJsonValue& target,
                           CrtJsonValue&& source,
                           CrtJsonValue::AllocatorType& allocator) {
  if (!source.IsObject()) {
    FTL_LOG(INFO) << "LinkImpl::MergeObject() - source is not an object "
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

void LinkImpl::ReadLinkData(const std::function<void()>& done) {
  story_storage_->ReadLinkData(link_path_,
                               [this, done](const fidl::String& json) {
                                 if (!json.is_null()) {
                                   doc_.Parse(json.get());
                                 }

                                 done();
                               });
}

void LinkImpl::WriteLinkData(const std::function<void()>& done) {
  write_link_data_(done);
}

void LinkImpl::WriteLinkDataImpl(const std::function<void()>& done) {
  story_storage_->WriteLinkData(link_path_, JsonValueToString(doc_), done);
}

void LinkImpl::DatabaseChanged(LinkConnection* const src) {
  // src is only used to compare its value. If the connection was
  // deleted before the callback is invoked, it will also be removed
  // from connections_.
  WriteLinkData([this, src] { NotifyWatchers(src); });
}

void LinkImpl::ValidateSchema(const char* const entry_point,
                              const CrtJsonPointer& pointer,
                              const std::string& json) {
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
      pointer.StringifyUriFragment(sbapipath);
      FTL_LOG(ERROR) << "Schema constraint violation in "
                     << EncodeLinkPath(link_path_) << ":" << std::endl
                     << "  Constraint " << sbpath.GetString() << "/"
                     << validator.GetInvalidSchemaKeyword() << std::endl
                     << "  Doc location: " << sbdoc.GetString() << std::endl
                     << "  API " << entry_point << std::endl
                     << "  API path " << sbapipath.GetString() << std::endl
                     << "  API json " << json << std::endl;
    }
  }
}

void LinkImpl::OnChange(const fidl::String& json) {
  // NOTE(jimbe) With rapidjson, the opposite check is more expensive,
  // O(n^2), so we won't do it for now. See case kObjectType in
  // operator==() in include/rapidjson/document.h.
  //
  //  if (doc_.Equals(json)) {
  //    return;
  //  }
  //
  // Since all json in a link was written by the same serializer, this
  // check is mostly accurate. This test has false negatives when only
  // order differs.
  if (json == JsonValueToString(doc_)) {
    return;
  }

  // TODO(jimbe): Decide how these changes should be merged into the current
  // CrtJsonDoc. In this first iteration, we'll do a wholesale replace.
  //
  // NOTE(mesch): This causes FW-208.
  doc_.Parse(json);

  // TODO(mesch): This does not notify WatchAll() watchers, because they are
  // registered with a null connection, and watchers on closed
  // connections. Introduced in
  // https://fuchsia-review.googlesource.com/#/c/36552/.
  NotifyWatchers(nullptr);
}

void LinkImpl::NotifyWatchers(LinkConnection* const src) {
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
                     ftl::WeakPtr<LinkConnection> conn) {
  LinkWatcherPtr watcher_ptr;
  watcher_ptr.Bind(std::move(watcher));

  // TODO(jimbe): We need to send an initial notification of state until there
  // is snapshot information that can be used by clients to query the state at
  // this instant. Otherwise there is no sequence information about total state
  // versus incremental changes.
  watcher_ptr->Notify(JsonValueToString(doc_));

  watchers_.emplace_back(std::make_unique<LinkWatcherConnection>(
      this, std::move(conn), std::move(watcher_ptr)));
}

LinkConnection::LinkConnection(LinkImpl* const impl,
                               fidl::InterfaceRequest<Link> link_request)
    : impl_(impl),
      binding_(this, std::move(link_request)),
      weak_ptr_factory_(this) {
  impl_->AddConnection(this);
  binding_.set_connection_error_handler(
      [this] { impl_->RemoveConnection(this); });
}

LinkConnection::~LinkConnection() = default;

void LinkConnection::Watch(fidl::InterfaceHandle<LinkWatcher> watcher) {
  // This watcher stays associated with the connection it was registered
  // through. The pointer is used to block notifications for updates that
  // originate at the same connection. If the connection goes away, the weak
  // pointer becomes null and protects against another LinkConnection getting
  // allocated at the same address.
  impl_->Watch(std::move(watcher), weak_ptr_factory_.GetWeakPtr());
}

void LinkConnection::WatchAll(fidl::InterfaceHandle<LinkWatcher> watcher) {
  // This watcher is not associated with the connection it was registered
  // through. The connection is recorded as null (see above for why it's a weak
  // pointer), which never equals any connection that originates an update, so
  // no update notification is ever blocked.
  impl_->Watch(std::move(watcher), ftl::WeakPtr<LinkConnection>());
}

void LinkConnection::Sync(const SyncCallback& callback) {
  impl_->Sync(callback);
}

void LinkConnection::SetSchema(const fidl::String& json_schema) {
  impl_->SetSchema(json_schema);
}

void LinkConnection::UpdateObject(fidl::Array<fidl::String> path,
                                  const fidl::String& json) {
  impl_->UpdateObject(std::move(path), json, this);
}

void LinkConnection::Set(fidl::Array<fidl::String> path,
                         const fidl::String& json) {
  impl_->Set(std::move(path), json, this);
}

void LinkConnection::Erase(fidl::Array<fidl::String> path) {
  impl_->Erase(std::move(path), this);
}

void LinkConnection::Get(fidl::Array<fidl::String> path,
                         const GetCallback& callback) {
  impl_->Get(std::move(path), callback);
}

LinkWatcherConnection::LinkWatcherConnection(LinkImpl* const impl,
                                             ftl::WeakPtr<LinkConnection> conn,
                                             LinkWatcherPtr watcher)
    : impl_(impl), conn_(std::move(conn)), watcher_(std::move(watcher)) {
  watcher_.set_connection_error_handler(
      [this] { impl_->RemoveConnection(this); });
}

LinkWatcherConnection::~LinkWatcherConnection() = default;

void LinkWatcherConnection::Notify(const fidl::String& value,
                                   LinkConnection* const src) {
  if (conn_.get() != src) {
    watcher_->Notify(value);
  }
}

}  // namespace modular
