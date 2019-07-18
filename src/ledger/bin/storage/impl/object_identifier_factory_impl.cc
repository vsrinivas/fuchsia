// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"

#include <sstream>
#include <string>

#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/public/types.h"

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
      ObjectIdentifierFactoryImpl* tracker,
      std::map<ObjectDigest, std::weak_ptr<ObjectIdentifier::Token>>::iterator map_entry)
      : tracker_(tracker), map_entry_(map_entry) {
    FXL_VLOG(1) << "ObjectIdentifier: start tracking " << map_entry_->first;
  }

  ~TokenImpl() override {
    FXL_VLOG(1) << "ObjectIdentifier: stop tracking " << map_entry_->first;
    FXL_DCHECK(tracker_->thread_checker_.IsCreationThreadCurrent());
    FXL_DCHECK(tracker_->dispatcher_checker_.IsCreationDispatcherCurrent());
    FXL_DCHECK(map_entry_->second.expired());

    tracker_->tokens_.erase(map_entry_);
  }

 private:
  ObjectIdentifierFactoryImpl* tracker_;
  std::map<ObjectDigest, std::weak_ptr<ObjectIdentifier::Token>>::iterator map_entry_;
};

ObjectIdentifierFactoryImpl::ObjectIdentifierFactoryImpl() = default;

ObjectIdentifierFactoryImpl::~ObjectIdentifierFactoryImpl() {
  FXL_DCHECK(tokens_.empty()) << TokenCountsToString(tokens_);
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
  auto token = std::make_shared<TokenImpl>(this, it);
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

ObjectIdentifier ObjectIdentifierFactoryImpl::MakeObjectIdentifier(uint32_t key_index,
                                                                   uint32_t deletion_scope_id,
                                                                   ObjectDigest object_digest) {
  if (GetObjectDigestInfo(object_digest).is_inlined()) {
    // Inlined objects do not need to be tracked.
    return ObjectIdentifier(key_index, deletion_scope_id, std::move(object_digest), nullptr);
  }
  auto token = GetToken(object_digest);
  return ObjectIdentifier(key_index, deletion_scope_id, std::move(object_digest), std::move(token));
}

}  // namespace storage
