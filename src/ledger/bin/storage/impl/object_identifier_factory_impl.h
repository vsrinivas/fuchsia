// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IDENTIFIER_FACTORY_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IDENTIFIER_FACTORY_IMPL_H_

#include <map>
#include <memory>

#include "lib/fit/function.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/synchronization/dispatcher_checker.h"
#include "src/lib/fxl/compiler_specific.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/synchronization/thread_checker.h"

namespace storage {

// A class to create and track object identifiers.
class ObjectIdentifierFactoryImpl : public ObjectIdentifierFactory {
 public:
  class TokenImpl;

  ObjectIdentifierFactoryImpl();
  ~ObjectIdentifierFactoryImpl();

  // This class is neither copyable nor movable. It is essential because object tokens reference it.
  ObjectIdentifierFactoryImpl(const ObjectIdentifierFactoryImpl&) = delete;
  ObjectIdentifierFactoryImpl& operator=(const ObjectIdentifierFactoryImpl&) = delete;

  // Returns the number of live object identifiers issued for |digest|.
  long count(const ObjectDigest& digest) const;

  // Returns the number of tracked identifiers.
  int size() const;

  // Sets a |callback| to be called every time the number of live object identifiers for an object
  // reaches 0.
  void SetUntrackedCallback(fit::function<void(const ObjectDigest&)> callback);

  // ObjectIdentifierFactory:
  // Returns an object identifier for the provided parameters. If the |object_digest| is currently
  // pending deletion, marks the deletion as aborted.
  ObjectIdentifier MakeObjectIdentifier(uint32_t key_index, ObjectDigest object_digest) override;

  bool MakeObjectIdentifierFromStorageBytes(convert::ExtendedStringView storage_bytes,
                                            ObjectIdentifier* object_identifier) override;
  std::string ObjectIdentifierToStorageBytes(const ObjectIdentifier& identifier) override;

  FXL_WARN_UNUSED_RESULT bool TrackDeletion(const ObjectDigest& object_digest) override;

  FXL_WARN_UNUSED_RESULT bool UntrackDeletion(const ObjectDigest& object_digest) override;

 private:
  // Marks the deletion of |object_digest| as aborted if the object is currently pending deletion.
  void AbortDeletion(const ObjectDigest& object_digest);

  // Returns a Token tracking |digest|.
  std::shared_ptr<ObjectIdentifier::Token> GetToken(ObjectDigest digest);

  // Current token for each live digest. Entries are cleaned up when the tokens expire.
  std::map<ObjectDigest, std::weak_ptr<ObjectIdentifier::Token>> tokens_;

  // Called every time the number of live object identifiers for an object reaches 0.
  fit::function<void(const ObjectDigest&)> on_untracked_object_;

  // Every key in the map is an object digest pending deletion. The value indicates whether the
  // deletion must be aborted or not.
  std::map<ObjectDigest, bool> deletion_aborted_;

  // To check for multithreaded accesses.
  fxl::ThreadChecker thread_checker_;
  ledger::DispatcherChecker dispatcher_checker_;

  // Must be the last member variable.
  fxl::WeakPtrFactory<ObjectIdentifierFactoryImpl> weak_factory_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IDENTIFIER_FACTORY_IMPL_H_
