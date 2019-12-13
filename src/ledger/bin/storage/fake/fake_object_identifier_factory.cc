// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"

#include <utility>

#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/memory/weak_ptr.h"

namespace storage {
namespace fake {

class FakeObjectIdentifierFactory::TokenImpl : public ObjectIdentifier::Token {
 public:
  explicit TokenImpl(ledger::WeakPtr<FakeObjectIdentifierFactory> factory) : factory_(factory) {}
  ~TokenImpl() override = default;
  ObjectIdentifierFactory* factory() const override { return factory_.get(); }

 private:
  ledger::WeakPtr<FakeObjectIdentifierFactory> factory_;
};

FakeObjectIdentifierFactory::FakeObjectIdentifierFactory() : weak_factory_(this) {}

FakeObjectIdentifierFactory::~FakeObjectIdentifierFactory() = default;

bool FakeObjectIdentifierFactory::IsLive(const ObjectDigest& digest) const {
  auto it = tokens_.find(digest);
  if (it == tokens_.end()) {
    return false;
  }
  // There is always at least one reference, in tokens_ itself. The object is live if there are
  // more.
  return it->second.use_count() > 1;
}

ObjectIdentifier FakeObjectIdentifierFactory::MakeObjectIdentifier(uint32_t key_index,
                                                                   ObjectDigest object_digest) {
  auto [it, inserted] =
      tokens_.emplace(object_digest, std::make_shared<TokenImpl>(weak_factory_.GetWeakPtr()));
  return ObjectIdentifier(key_index, std::move(object_digest), it->second);
}

bool FakeObjectIdentifierFactory::MakeObjectIdentifierFromStorageBytes(
    convert::ExtendedStringView storage_bytes, ObjectIdentifier* object_identifier) {
  return storage::DecodeObjectIdentifier(convert::ToStringView(storage_bytes), this,
                                         object_identifier);
}

std::string FakeObjectIdentifierFactory::ObjectIdentifierToStorageBytes(
    const ObjectIdentifier& identifier) {
  return storage::EncodeObjectIdentifier(identifier);
}

bool FakeObjectIdentifierFactory::TrackDeletion(const ObjectDigest& object_digest) { return false; }

bool FakeObjectIdentifierFactory::UntrackDeletion(const ObjectDigest& object_digest) {
  return false;
}

}  // namespace fake
}  // namespace storage
