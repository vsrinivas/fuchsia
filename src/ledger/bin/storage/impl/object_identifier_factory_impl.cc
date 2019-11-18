// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"

#include <sstream>
#include <string>

#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace storage {
namespace {

// Converts a map of ObjectDigest counts to a string listing them.
std::string TokenCountsToString(
    const std::map<ObjectDigest, std::weak_ptr<ObjectIdentifier::Token>>& tokens) {
  std::ostringstream stream;
  for (const auto& token : tokens) {
    stream << "\n" << token.first << " " << token.second.use_count();
  }
  return stream.str();
}

}  // namespace

// Token implementation that cleans up its entry in the the token map upon destruction.
class ObjectIdentifierFactoryImpl::TokenImpl : public ObjectIdentifier::Token {
 public:
  explicit TokenImpl(
      fxl::WeakPtr<ObjectIdentifierFactoryImpl> tracker,
      std::map<ObjectDigest, std::weak_ptr<ObjectIdentifier::Token>>::iterator map_entry)
      : tracker_(std::move(tracker)), map_entry_(map_entry) {}

  ~TokenImpl() override {
    if (!tracker_) {
      FXL_VLOG(1) << "ObjectIdentifier: stop tracking an object after the factory was destructed";
      return;
    }

    ObjectDigest object_digest = map_entry_->first;
    FXL_VLOG(2) << "ObjectIdentifier: stop tracking " << object_digest;
    FXL_DCHECK(tracker_->thread_checker_.IsCreationThreadCurrent());
    FXL_DCHECK(tracker_->dispatcher_checker_.IsCreationDispatcherCurrent());
    FXL_DCHECK(map_entry_->second.expired());

    tracker_->tokens_.erase(map_entry_);
    // Check if we need to notify the on_untracked_object_ callback.
    if (!tracker_->on_untracked_object_) {
      return;
    }
    switch (tracker_->notification_policy_) {
      case NotificationPolicy::NEVER:
        break;
      case NotificationPolicy::ALWAYS:
        tracker_->on_untracked_object_(object_digest);
        break;
      case NotificationPolicy::ON_MARKED_OBJECTS_ONLY:
        auto it = tracker_->to_notify_.find(object_digest);
        if (it != tracker_->to_notify_.end()) {
          tracker_->to_notify_.erase(it);
          tracker_->on_untracked_object_(object_digest);
        }
    }
  }

  ObjectIdentifierFactory* factory() const override { return tracker_.get(); }

 private:
  fxl::WeakPtr<ObjectIdentifierFactoryImpl> tracker_;
  std::map<ObjectDigest, std::weak_ptr<ObjectIdentifier::Token>>::iterator map_entry_;
};

ObjectIdentifierFactoryImpl::ObjectIdentifierFactoryImpl(NotificationPolicy notification_policy)
    : notification_policy_(notification_policy), weak_factory_(this) {}

ObjectIdentifierFactoryImpl::~ObjectIdentifierFactoryImpl() {
  if (!tokens_.empty()) {
    FXL_VLOG(1) << "Destructing ObjectIdentifierFactory with remaining live tokens: "
                << TokenCountsToString(tokens_);
  }
}

std::shared_ptr<ObjectIdentifier::Token> ObjectIdentifierFactoryImpl::GetToken(
    ObjectDigest digest) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(dispatcher_checker_.IsCreationDispatcherCurrent());

  auto [it, created] = tokens_.try_emplace(std::move(digest));
  if (!created) {
    FXL_DCHECK(!it->second.expired());
    return it->second.lock();
  }
  FXL_VLOG(2) << "ObjectIdentifier: start tracking " << it->first;
  auto token = std::make_shared<TokenImpl>(weak_factory_.GetWeakPtr(), it);
  it->second = token;
  FXL_DCHECK(it->second.use_count() == 1);
  return token;
}

long ObjectIdentifierFactoryImpl::count(const ObjectDigest& digest) const {
  auto it = tokens_.find(digest);
  if (it == tokens_.end()) {
    return 0;
  }
  return it->second.use_count();
}

int ObjectIdentifierFactoryImpl::size() const { return tokens_.size(); }

void ObjectIdentifierFactoryImpl::SetUntrackedCallback(
    fit::function<void(const ObjectDigest&)> callback) {
  on_untracked_object_ = std::move(callback);
}

void ObjectIdentifierFactoryImpl::NotifyOnUntracked(ObjectDigest object_digest) {
  if (notification_policy_ != NotificationPolicy::ON_MARKED_OBJECTS_ONLY) {
    return;
  }
  if (on_untracked_object_ && tokens_.find(object_digest) == tokens_.end()) {
    // There are no live references to this object, call the callback directly.
    on_untracked_object_(object_digest);
  } else {
    to_notify_.insert(std::move(object_digest));
  }
}

bool ObjectIdentifierFactoryImpl::TrackDeletion(const ObjectDigest& object_digest) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(dispatcher_checker_.IsCreationDispatcherCurrent());

  if (tokens_.find(object_digest) != tokens_.end()) {
    // The object is tracked currently.
    return false;
  }
  if (!deletion_aborted_.emplace(object_digest, false).second) {
    // The object is already pending deletion.
    return false;
  }
  FXL_VLOG(1) << "Start deletion " << object_digest;
  return true;
}

void ObjectIdentifierFactoryImpl::AbortDeletion(const ObjectDigest& object_digest) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(dispatcher_checker_.IsCreationDispatcherCurrent());

  auto it = deletion_aborted_.find(object_digest);
  if (it == deletion_aborted_.end()) {
    // The object is not pending deletion.
    return;
  }
  FXL_VLOG(1) << "Abort deletion " << object_digest;
  it->second = true;
}

bool ObjectIdentifierFactoryImpl::UntrackDeletion(const ObjectDigest& object_digest) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(dispatcher_checker_.IsCreationDispatcherCurrent());

  auto it = deletion_aborted_.find(object_digest);
  if (it == deletion_aborted_.end()) {
    // The object is not pending deletion.
    FXL_NOTREACHED() << "Unbalanced calls to start and abort deletion of object " << object_digest;
    return false;
  }
  FXL_VLOG(1) << "Complete deletion " << object_digest;
  const bool deletion_aborted = it->second;
  deletion_aborted_.erase(it);
  return !deletion_aborted;
}

ObjectIdentifier ObjectIdentifierFactoryImpl::MakeObjectIdentifier(uint32_t key_index,
                                                                   ObjectDigest object_digest) {
  // Creating an object identifier automatically aborts any pending deletion on the object.
  AbortDeletion(object_digest);
  auto token = GetToken(object_digest);
  return ObjectIdentifier(key_index, std::move(object_digest), std::move(token));
}

bool ObjectIdentifierFactoryImpl::MakeObjectIdentifierFromStorageBytes(
    convert::ExtendedStringView storage_bytes, ObjectIdentifier* object_identifier) {
  if (!DecodeObjectIdentifier(convert::ToStringView(storage_bytes), this, object_identifier)) {
    return false;
  }
  return IsDigestValid(object_identifier->object_digest());
}

std::string ObjectIdentifierFactoryImpl::ObjectIdentifierToStorageBytes(
    const ObjectIdentifier& identifier) {
  return EncodeObjectIdentifier(identifier);
}

}  // namespace storage
