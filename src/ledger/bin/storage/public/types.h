// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_TYPES_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_TYPES_H_

#include <map>
#include <ostream>
#include <set>
#include <string>
#include <variant>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/clocks/public/types.h"
#include "src/ledger/bin/public/status.h"
#include "src/lib/fxl/compiler_specific.h"
#include "src/lib/fxl/strings/string_view.h"

namespace storage {

using PageId = std::string;
using PageIdView = convert::ExtendedStringView;
using CommitId = std::string;
using CommitIdView = convert::ExtendedStringView;
using EntryId = std::string;

// The type of object.
// Ledger stores user created content on BTrees, where the nodes (TREE_NODE
// objects) store the user-created keys and references to the user-created
// values. The content of the values is (usually) stored into separate BLOB
// objects.
// See ledger/storage/impl/btree for more details.
enum class ObjectType {
  // A |TreeNode| object.
  TREE_NODE,
  // An opaque sequence of bytes. Currently used to store values.
  BLOB,
};

// The digest of an object.
// This class is a container for an object digest, treated as an opaque blob. It
// is not responsible for computing or validating the digest; see
// storage/impl/object_digest.h for such functions.
class ObjectDigest {
 public:
  // Builds an invalid object digest. Useful, eg., when returning a default
  // object upon error (with a failed status).
  ObjectDigest();

  // Builds a valid object digest whose value is equal to |digest|.
  explicit ObjectDigest(std::string digest);
  explicit ObjectDigest(const flatbuffers::Vector<uint8_t>* digest);

  ObjectDigest(const ObjectDigest&);
  ObjectDigest& operator=(const ObjectDigest&);
  ObjectDigest(ObjectDigest&&) noexcept;
  ObjectDigest& operator=(ObjectDigest&&) noexcept;

  // Returns whether this object represents a valid object digest.
  bool IsValid() const;

  // Returns the content of the object digest.
  // The reference is valid as long as this object. Must only be called if the
  // object is valid.
  const std::string& Serialize() const;

 private:
  friend bool operator==(const ObjectDigest& lhs, const ObjectDigest& rhs);
  friend bool operator<(const ObjectDigest& lhs, const ObjectDigest& rhs);

  std::optional<std::string> digest_;
};

bool operator==(const ObjectDigest& lhs, const ObjectDigest& rhs);
bool operator!=(const ObjectDigest& lhs, const ObjectDigest& rhs);
bool operator<(const ObjectDigest& lhs, const ObjectDigest& rhs);
std::ostream& operator<<(std::ostream& os, const ObjectDigest& e);

// The priority at which the key value is downloaded, and the cache policy.
enum class KeyPriority {
  EAGER,
  LAZY,
};

// The identifier of an object. This contains the digest of the object, as well as the information
// needed to hide its name and encrypt its content, and a token to track live object identifiers.
class ObjectIdentifierFactory;
class ObjectIdentifier {
 public:
  // A token that ensures that the associated object remains available as long as the token object
  // is alive.
  class Token {
   public:
    Token() = default;
    // Purely virtual to make the class abstract.
    virtual ~Token() = 0;
    Token(const Token&) = delete;
    Token& operator=(const Token&) = delete;

    // The factory that emitted this token, or nullptr if the factory has been destructed.
    virtual ObjectIdentifierFactory* factory() const = 0;
  };

  // Constructs an empty, untracked object identifier.
  ObjectIdentifier();

  // Constructs an object identifier. If |token| is nullptr, the object is untracked.
  ObjectIdentifier(uint32_t key_index, ObjectDigest object_digest, std::shared_ptr<Token> token);

  ObjectIdentifier(const ObjectIdentifier&);
  ObjectIdentifier& operator=(const ObjectIdentifier&);
  ObjectIdentifier(ObjectIdentifier&&) noexcept;
  ObjectIdentifier& operator=(ObjectIdentifier&&) noexcept;

  uint32_t key_index() const { return key_index_; }
  const ObjectDigest& object_digest() const { return object_digest_; }
  // Returns the factory that currently tracks this object identifier. Returns nullptr if untracked,
  // either because the factory expired or because the identifier was never tracked.
  ObjectIdentifierFactory* factory() const { return token_ ? token_->factory() : nullptr; }

 private:
  friend bool operator==(const ObjectIdentifier&, const ObjectIdentifier&);
  friend bool operator<(const ObjectIdentifier&, const ObjectIdentifier&);

  uint32_t key_index_;
  ObjectDigest object_digest_;
  std::shared_ptr<Token> token_;
};

bool operator==(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs);
bool operator!=(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs);
bool operator<(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs);
std::ostream& operator<<(std::ostream& os, const ObjectIdentifier& e);

// A factory interface to build object identifiers.
//
// In addition to allocating and serializing object identifiers, this class also allows to keep
// track of objects that are "pending deletion". Because object deletion requires a number of checks
// that are not atomic, it is necessary to register the intent to delete an object before
// proceeding, and then check that this object has not been accessed or referenced concurrently (ie.
// that no identifier has been issued for this object) when ready to issue the final call to perform
// the actual deletion from the database.
class ObjectIdentifierFactory {
 public:
  // Creates an object identifier.
  // This function must called only from the thread that created this |ObjectIdentifierFactory|.
  // Destruction of the returned identifier must happen on the same thread too.
  virtual ObjectIdentifier MakeObjectIdentifier(uint32_t key_index, ObjectDigest object_digest) = 0;

  // Creates an object identifier from its serialization.
  // This function must called only from the thread that created this |ObjectIdentifierFactory|.
  // Destruction of the returned identifier must happen on the same thread too.
  virtual bool MakeObjectIdentifierFromStorageBytes(convert::ExtendedStringView storage_bytes,
                                                    ObjectIdentifier* object_identifier) = 0;

  // Serializes an object identifier.
  virtual std::string ObjectIdentifierToStorageBytes(const ObjectIdentifier& identifier) = 0;

  // Registers |object_digest| as pending deletion and returns true if there is currently no object
  // identifier for this digest and it is not already pending deletion. Returns false otherwise
  // (which means that deletion cannot proceed safely).
  FXL_WARN_UNUSED_RESULT virtual bool TrackDeletion(const ObjectDigest& object_digest) = 0;

  // Marks the deletion of |object_digest| as complete and returns true if the object was currently
  // pending deletion and the deletion was not aborted already. Returns false otherwise (which means
  // that deletion cannot proceed safely).
  FXL_WARN_UNUSED_RESULT virtual bool UntrackDeletion(const ObjectDigest& object_digest) = 0;
};

// Object-object references, for garbage collection.
// For a given object |A|, contains a pair (|B|, |priority|) for every reference
// from |A| to |B| with the associated |priority|. Object digests must never
// represent inline pieces.
using ObjectReferencesAndPriority = std::set<std::pair<ObjectDigest, KeyPriority>>;

// An entry in a commit.
struct Entry {
  std::string key;
  ObjectIdentifier object_identifier;
  KeyPriority priority;
  EntryId entry_id;
};

bool operator==(const Entry& lhs, const Entry& rhs);
bool operator!=(const Entry& lhs, const Entry& rhs);
std::ostream& operator<<(std::ostream& os, const Entry& e);

// A change between two commit contents.
struct EntryChange {
  Entry entry;
  bool deleted;
};

bool operator==(const EntryChange& lhs, const EntryChange& rhs);
bool operator!=(const EntryChange& lhs, const EntryChange& rhs);
std::ostream& operator<<(std::ostream& os, const EntryChange& e);

// A change between 2 commit contents. |base| contains the previous contents for the same key and
// |target| the updated ones. In case of insertion |base| is null. Similarly, |target| is null in
// case of deletion.
struct TwoWayChange {
  std::unique_ptr<Entry> base;
  std::unique_ptr<Entry> target;
};

// A change between 3 commit contents.
struct ThreeWayChange {
  std::unique_ptr<Entry> base;
  std::unique_ptr<Entry> left;
  std::unique_ptr<Entry> right;
};

bool operator==(const ThreeWayChange& lhs, const ThreeWayChange& rhs);
bool operator!=(const ThreeWayChange& lhs, const ThreeWayChange& rhs);
std::ostream& operator<<(std::ostream& os, const ThreeWayChange& e);

enum class ChangeSource { LOCAL, P2P, CLOUD };
enum class IsObjectSynced : bool { NO, YES };

enum class JournalContainsClearOperation { NO, YES };

using Status = ledger::Status;

enum class CommitPruningPolicy {
  // Commits are never pruned.
  NEVER,
  // Commits are pruned as soon as possible, based on the local state only. Do not use this policy
  // if the device is synchronizing with other devices.
  LOCAL_IMMEDIATE,
};

enum class GarbageCollectionPolicy {
  // Local objects are never collected.
  NEVER,
  // Local objects are collected as soon as their in-memory reference count reaches zero.
  // This triggers many disk reads to check on-disk references every time an object is dropped, and
  // does not scan the database to collect unused objects.
  // Do not use this policy if you care about performance; mostly useful to find garbage-collection
  // bugs in tests.
  EAGER_LIVE_REFERENCES,
};

enum class DiffCompatibilityPolicy {
  // Tree nodes are uploaded to the cloud and storage falls back to getting objects from the cloud
  // if a tree cannot be obtained by diffs.
  USE_DIFFS_AND_TREE_NODES,
  // Tree nodes are not uploaded nor downloaded from the cloud: diffs must be available.
  USE_ONLY_DIFFS,
};

// A clock entry, for a single device.
struct ClockEntry {
  CommitId commit_id;
  uint64_t generation;
};

bool operator==(const ClockEntry& lhs, const ClockEntry& rhs);
bool operator!=(const ClockEntry& lhs, const ClockEntry& rhs);
std::ostream& operator<<(std::ostream& os, const ClockEntry& e);

// Entry for an active device in the page clock.
struct DeviceEntry {
  // Latest known unique local head of the device.
  ClockEntry head;
  // Latest known unique head of the device in the cloud.
  std::optional<ClockEntry> cloud;
};

bool operator==(const DeviceEntry& lhs, const DeviceEntry& rhs);
bool operator!=(const DeviceEntry& lhs, const DeviceEntry& rhs);
std::ostream& operator<<(std::ostream& os, const DeviceEntry& e);

// Clock tombstone for a specific device
// We know the device no longer possesses the page. This state may be stored so that
// other devices can be informed.
class ClockTombstone : public std::monostate {};
// Clock deletion
// All references to this device should be removed from storage.
class ClockDeletion : public std::monostate {};

// The entry for one device in the page clock.
using DeviceClock = std::variant<DeviceEntry, ClockTombstone, ClockDeletion>;

// A full clock, for all devices interested in a page and the Cloud.
using Clock = std::map<clocks::DeviceId, DeviceClock>;

}  // namespace storage
#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_TYPES_H_
