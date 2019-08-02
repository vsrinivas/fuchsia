// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"

#include <utility>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace storage {
namespace fake {

class FakeObjectIdentifierFactory::TokenImpl : public ObjectIdentifier::Token {
 public:
  TokenImpl(fxl::WeakPtr<FakeObjectIdentifierFactory> factory) : factory_(factory) {}
  ~TokenImpl() override = default;
  ObjectIdentifierFactory* factory() const override { return factory_.get(); }

 private:
  fxl::WeakPtr<FakeObjectIdentifierFactory> factory_;
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
                                                                   uint32_t deletion_scope_id,
                                                                   ObjectDigest object_digest) {
  auto [it, inserted] =
      tokens_.emplace(object_digest, std::make_shared<TokenImpl>(weak_factory_.GetWeakPtr()));
  return ObjectIdentifier(key_index, deletion_scope_id, std::move(object_digest), it->second);
}

}  // namespace fake
}  // namespace storage
