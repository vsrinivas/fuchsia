// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IDENTIFIER_FACTORY_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IDENTIFIER_FACTORY_IMPL_H_

#include <lib/fit/function.h>

#include <map>
#include <memory>

#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/synchronization/dispatcher_checker.h"
#include "src/ledger/bin/synchronization/thread_checker.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "third_party/abseil-cpp/absl/base/attributes.h"

namespace storage {

// A class to create and track object identifiers.
class ObjectIdentifierFactoryImpl : public ObjectIdentifierFactory {
 public:
  // The NotificationPolicy determines how notifications are sent out once an object becomes
  // untracked, i.e. has 0 live references.
  enum class NotificationPolicy {
    // No notifications will be sent. Using a |NEVER| NotificationPolicy is equivalent to never
    // setting the untracked callback.
    NEVER,
    // Notifications will only be sent for objects that have been marked. See |NotifyOnUtracked|.
    ON_MARKED_OBJECTS_ONLY,
    // Notifications are sent for all objects.
    ALWAYS,
  };

  class TokenImpl;

  // Note that |NotificationPolicy::ALWAYS| corresponds to the EAGER_LIVE_REFERENCES Garbage
  // Collection policy, and it is the default one here, because the default testing Garbage
  // Collection policy is EAGER_LIVE_REFERENCES. If |ledger::kDefaultGarbageCollectionPolicy|
  // changes, the default value of |notification_policy| should change as well.
  ObjectIdentifierFactoryImpl(NotificationPolicy notification_policy = NotificationPolicy::ALWAYS);
  ~ObjectIdentifierFactoryImpl();

  // This class is neither copyable nor movable. It is essential because object tokens reference it.
  ObjectIdentifierFactoryImpl(const ObjectIdentifierFactoryImpl&) = delete;
  ObjectIdentifierFactoryImpl& operator=(const ObjectIdentifierFactoryImpl&) = delete;

  // Returns the number of live object identifiers issued for |digest|.
  long count(const ObjectDigest& digest) const;

  // Returns the number of tracked identifiers.
  int size() const;

  // Sets a |callback| to be called every time the number of live object identifiers for an object
  // reaches 0. Whether this callback is called on an untracked object, depends on the
  // |notification_policy| set on the constructor. If the policy is:
  // - |NEVER|, this callback is ignored. Setting it has no effect.
  // - |ON_MARKED_OBJECTS_ONLY|, this callback is called only for objects for which the
  //   |NotifyOnUntracked| method has been called.
  // - |ALWAYS|, this callback is called on all objects
  void SetUntrackedCallback(fit::function<void(const ObjectDigest&)> callback);

  // This method only has an effect if called using the |ON_MARKED_OBJECTS_ONLY| policy. For others,
  // it is ignored. Once the object with the given |object_digest| becomes untracked, the untracked
  // callback will be called on it (if set). If the corresponding object already has 0 live
  // references, the callback is called immediately.
  void NotifyOnUntracked(ObjectDigest object_digest);

  // ObjectIdentifierFactory:
  // Returns an object identifier for the provided parameters. If the |object_digest| is currently
  // pending deletion, marks the deletion as aborted.
  ObjectIdentifier MakeObjectIdentifier(uint32_t key_index, ObjectDigest object_digest) override;

  bool MakeObjectIdentifierFromStorageBytes(convert::ExtendedStringView storage_bytes,
                                            ObjectIdentifier* object_identifier) override;
  std::string ObjectIdentifierToStorageBytes(const ObjectIdentifier& identifier) override;

  ABSL_MUST_USE_RESULT bool TrackDeletion(const ObjectDigest& object_digest) override;

  ABSL_MUST_USE_RESULT bool UntrackDeletion(const ObjectDigest& object_digest) override;

 private:
  // Marks the deletion of |object_digest| as aborted if the object is currently pending deletion.
  void AbortDeletion(const ObjectDigest& object_digest);

  // Returns a Token tracking |digest|.
  std::shared_ptr<ObjectIdentifier::Token> GetToken(ObjectDigest digest);

  // Current token for each live digest. Entries are cleaned up when the tokens expire.
  std::map<ObjectDigest, std::weak_ptr<ObjectIdentifier::Token>> tokens_;

  NotificationPolicy notification_policy_;

  // The set of objects to notify when their number of live object identifiers reaches 0.
  std::set<ObjectDigest> to_notify_;

  // Called every time the number of live object identifiers for a marked object reaches 0.
  fit::function<void(const ObjectDigest&)> on_untracked_object_;

  // Every key in the map is an object digest pending deletion. The value indicates whether the
  // deletion must be aborted or not.
  std::map<ObjectDigest, bool> deletion_aborted_;

  // To check for multithreaded accesses.
  ledger::ThreadChecker thread_checker_;
  ledger::DispatcherChecker dispatcher_checker_;

  // Must be the last member variable.
  ledger::WeakPtrFactory<ObjectIdentifierFactoryImpl> weak_factory_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IDENTIFIER_FACTORY_IMPL_H_
