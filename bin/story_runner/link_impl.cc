// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/link_impl.h"

#include <functional>

#include "apps/modular/services/story/link.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

namespace modular {

LinkImpl::LinkImpl(StoryStoragePtr story_storage,
                   const fidl::String& name,
                   fidl::InterfaceRequest<Link> link_request)
    : name_(name),
      story_storage_(std::move(story_storage)),
      write_link_data_(Bottleneck::FRONT, this, &LinkImpl::WriteLinkDataImpl) {
  ReadLinkData(ftl::MakeCopyable([
    this, link_request = std::move(link_request)
  ]() mutable { LinkConnection::New(this, std::move(link_request)); }));
}

// The |LinkConnection| object knows which client made the call to Set() or
// Update(), so it notifies either all clients or all other clients, depending
// on whether WatchAll() or Watch() was called, respectively.
//
// TODO(jimbe) This mechanism breaks if the call to Watch() is made
// *after* the call to SetAllDocument(). Need to find a way to improve
// this.

void LinkImpl::Set(const fidl::String& path,
                   const fidl::String& json,
                   LinkConnection* const src) {
  //  FTL_LOG(INFO) << "**** Set()" << std::endl
  //                << "PATH " << path << std::endl
  //                << "JSON " << json;
  CrtJsonDoc new_value;
  new_value.Parse(json);
  if (new_value.HasParseError()) {
    // TODO(jimbe) Handle errors better
    FTL_CHECK(!new_value.HasParseError())
        << "PARSE ERROR in Update()" << new_value.GetParseError();
    return;
  }

  bool dirty = true;
  bool alreadyExist = false;

  // A single slash creates an unwanted node at the root.
  CrtJsonPointer ptr(path == "/" ? "" : path);
  CrtJsonValue& current_value =
      ptr.Create(doc_, doc_.GetAllocator(), &alreadyExist);
  if (alreadyExist) {
    dirty = new_value != current_value;
  }

  if (dirty) {
    ptr.Set(doc_, new_value);
    DatabaseChanged(src);
  }
  FTL_LOG(INFO) << "LinkImpl::Set() " << JsonValueToPrettyString(doc_);
}

void LinkImpl::UpdateObject(const fidl::String& path,
                            const fidl::String& json,
                            LinkConnection* const src) {
  //  FTL_LOG(INFO) << "**** Update()" << std::endl
  //                << "PATH " << path << std::endl
  //                << "JSON " << json;
  CrtJsonDoc new_value;
  new_value.Parse(json);
  if (new_value.HasParseError()) {
    // TODO(jimbe) Handle errors better
    FTL_CHECK(!new_value.HasParseError())
        << "PARSE ERROR in Update()" << new_value.GetParseError();
    return;
  }

  CrtJsonPointer ptr(path);
  CrtJsonValue& current_value = ptr.Create(doc_);

  bool dirty =
      MergeObject(current_value, std::move(new_value), doc_.GetAllocator());
  if (dirty) {
    DatabaseChanged(src);
  }
  FTL_LOG(INFO) << "LinkImpl::Update() " << JsonValueToPrettyString(doc_);
}

void LinkImpl::Erase(const fidl::String& path, LinkConnection* const src) {
  CrtJsonPointer ptr(path);
  auto p = ptr.Get(doc_);
  if (p != nullptr && ptr.Erase(doc_)) {
    DatabaseChanged(src);
  }
}

void LinkImpl::Sync(const std::function<void()>& callback) {
  story_storage_->Sync(callback);
}

// Merge source into target. The values will be move()'d.
// Returns true if the merge operation caused any changes.
bool LinkImpl::MergeObject(CrtJsonValue& target,
                           CrtJsonValue&& source,
                           CrtJsonValue::AllocatorType& allocator) {
  FTL_CHECK(source.IsObject());

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
  story_storage_->ReadLinkData(name_, [this, done](LinkDataPtr data) {
    if (!data.is_null()) {
      std::string json;
      data->json.Swap(&json);
      doc_.Parse(std::move(json));
      FTL_LOG(INFO) << "LinkImpl::ReadLinkData() " << JsonValueToPrettyString(doc_);
    }

    done();
  });
}

void LinkImpl::WriteLinkData(const std::function<void()>& done) {
  write_link_data_(done);
}

void LinkImpl::WriteLinkDataImpl(const std::function<void()>& done) {
  auto link_data = LinkData::New();
  link_data->json = JsonValueToString(doc_);
  story_storage_->WriteLinkData(name_, std::move(link_data), done);
}

void LinkImpl::DatabaseChanged(LinkConnection* const src) {
  // src is only used to compare its value. If the connection was
  // deleted before the callback is invoked, it will also be removed
  // from connections_.
  WriteLinkData([this, src] { NotifyWatchers(src); });
}

void LinkImpl::OnChange(LinkDataPtr link_data) {
  // TODO(jimbe) With rapidjson, this check is expensive, O(n^2), so we won't
  // do it for now. See case kObjectType in operator==() in
  // include/rapidjson/document.h.
  //  if (doc_.Equals(link_data->json)) {
  //    return;
  //  }

  // TODO(jimbe) Decide how these changes should be merged into the current
  // CrtJsonDoc. In this first iteration, we'll do a wholesale replace.
  doc_.Parse(link_data->json);
  NotifyWatchers(nullptr);
}

void LinkImpl::NotifyWatchers(LinkConnection* const src) {
  for (auto& dst : connections_) {
    const bool self_notify = (dst.get() != src);
    dst->NotifyWatchers(doc_, self_notify);
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

  if (connections_.empty() && orphaned_handler_) {
    orphaned_handler_();
  }
}

LinkConnection::LinkConnection(LinkImpl* const impl,
                               fidl::InterfaceRequest<Link> link_request)
    : impl_(impl), binding_(this, std::move(link_request)) {
  impl_->AddConnection(this);
  binding_.set_connection_error_handler(
      [this] { impl_->RemoveConnection(this); });
}

void LinkConnection::Watch(fidl::InterfaceHandle<LinkWatcher> watcher) {
  AddWatcher(std::move(watcher), false);
}

void LinkConnection::WatchAll(fidl::InterfaceHandle<LinkWatcher> watcher) {
  AddWatcher(std::move(watcher), true);
}

void LinkConnection::AddWatcher(fidl::InterfaceHandle<LinkWatcher> watcher,
                                const bool self_notify) {
  LinkWatcherPtr watcher_ptr;
  watcher_ptr.Bind(std::move(watcher));

  // TODO(jimbe) We need to send an initial notification of state until
  // there is snapshot information that can be used by clients to query the
  // state at this instant. Otherwise there is no sequence information about
  // total state versus incremental changes.
  auto& doc = impl_->doc();
  watcher_ptr->Notify(JsonValueToString(doc));

  auto& watcher_set = self_notify ? all_watchers_ : watchers_;
  watcher_set.AddInterfacePtr(std::move(watcher_ptr));
}

void LinkConnection::NotifyWatchers(const CrtJsonDoc& doc,
                                    const bool self_notify) {
  fidl::String json(JsonValueToString(impl_->doc()));

  if (self_notify) {
    watchers_.ForAllPtrs(
        [&json](LinkWatcher* const watcher) { watcher->Notify(json); });
  }
  all_watchers_.ForAllPtrs(
      [&json](LinkWatcher* const watcher) { watcher->Notify(json); });
}

void LinkConnection::Dup(fidl::InterfaceRequest<Link> dup) {
  LinkConnection::New(impl_, std::move(dup));
}

void LinkConnection::Sync(const SyncCallback& callback) {
  impl_->Sync(callback);
}

void LinkConnection::UpdateObject(const fidl::String& path,
                                  const fidl::String& json) {
  impl_->UpdateObject(path, json, this);
}

void LinkConnection::Set(const fidl::String& path, const fidl::String& json) {
  impl_->Set(path, json, this);
}

void LinkConnection::Erase(const fidl::String& path) {
  impl_->Erase(path, this);
}

void LinkConnection::Get(const fidl::String& path,
                         const GetCallback& callback) {
  CrtJsonPointer ptr(path);
  auto p = ptr.Get(impl_->doc());
  if (p == nullptr) {
    callback(fidl::String());
  } else {
    callback(fidl::String(JsonValueToString(*p)));
  }
}

}  // namespace modular
