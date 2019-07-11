// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_PIECE_TRACKER_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_PIECE_TRACKER_H_

#include <map>
#include <memory>

#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/synchronization/dispatcher_checker.h"
#include "src/lib/fxl/synchronization/thread_checker.h"

namespace storage {

// A class to create and track piece tokens.
class PieceTracker {
 public:
  class PieceTokenImpl;

  PieceTracker();
  ~PieceTracker();

  // This class is neither copyable nor movable. It is essential because object tokens reference it.
  PieceTracker(const PieceTracker&) = delete;
  PieceTracker& operator=(const PieceTracker&) = delete;

  // Returns a PieceToken tracking |identifier|.
  // This function must called only from the thread that created this |PieceTracker|.
  // Destruction of the returned token must happen on the same thread too, and happen before the
  // |PieceTracker| instance is destroyed.
  std::shared_ptr<PieceToken> GetPieceToken(ObjectIdentifier identifier);

  // Returns the number of live tokens issued for |identifier|.
  int count(const ObjectIdentifier& identifier) const;

  // Returns the number of tracked identifiers.
  int size() const;

 private:
  // Number of live tokens per identifier. Entries are cleaned up when the count
  // reaches zero.
  std::map<ObjectIdentifier, std::weak_ptr<PieceToken>> tokens_;

  // To check for multithreaded accesses.
  fxl::ThreadChecker thread_checker_;
  ledger::DispatcherChecker dispatcher_checker_;
};

// Token that does not hold a reference, when it is safe to discard the piece
// but a token needs to be returned.
class DiscardableToken : public PieceToken {
 public:
  DiscardableToken(ObjectIdentifier identifier);
  const ObjectIdentifier& GetIdentifier() const override;

 private:
  ObjectIdentifier identifier_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_PIECE_TRACKER_H_
