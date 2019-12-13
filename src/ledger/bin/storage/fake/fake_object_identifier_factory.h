// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_IDENTIFIER_FACTORY_H_
#define SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_IDENTIFIER_FACTORY_H_

#include <map>
#include <memory>

#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "third_party/abseil-cpp/absl/base/attributes.h"

namespace storage {
namespace fake {

// A fake class to create and track object identifiers, which leaks memory but is good enough for
// tests.
class FakeObjectIdentifierFactory : public ObjectIdentifierFactory {
 public:
  class TokenImpl;

  FakeObjectIdentifierFactory();
  ~FakeObjectIdentifierFactory();

  FakeObjectIdentifierFactory(const FakeObjectIdentifierFactory&) = delete;
  FakeObjectIdentifierFactory& operator=(const FakeObjectIdentifierFactory&) = delete;

  // Returns whether there are any live ObjectIdentifier for |digest|.
  bool IsLive(const ObjectDigest& digest) const;

  // ObjectIdentifierFactory:
  ObjectIdentifier MakeObjectIdentifier(uint32_t key_index, ObjectDigest object_digest) override;
  bool MakeObjectIdentifierFromStorageBytes(convert::ExtendedStringView storage_bytes,
                                            ObjectIdentifier* object_identifier) override;
  std::string ObjectIdentifierToStorageBytes(const ObjectIdentifier& identifier) override;

  ABSL_MUST_USE_RESULT bool TrackDeletion(const ObjectDigest& object_digest) override;

  ABSL_MUST_USE_RESULT bool UntrackDeletion(const ObjectDigest& object_digest) override;

 private:
  // Token for each digest. Entries are never cleaned up, the count stays to at least 1 because the
  // map retains a reference.
  std::map<ObjectDigest, std::shared_ptr<ObjectIdentifier::Token>> tokens_;

  ledger::WeakPtrFactory<FakeObjectIdentifierFactory> weak_factory_;
};

}  // namespace fake
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_IDENTIFIER_FACTORY_H_
