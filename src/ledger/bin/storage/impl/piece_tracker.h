// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_PIECE_TRACKER_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_PIECE_TRACKER_H_

#include <memory>
#include <set>

#include "lib/fit/function.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {

// A class to create and track piece tokens.
class PieceTracker {
 public:
  PieceTracker();
  ~PieceTracker();

  // This class is neither copyable nor movable. It is essential because object
  // tokens reference it.
  PieceTracker(const PieceTracker&) = delete;
  PieceTracker& operator=(const PieceTracker&) = delete;

  // Returns an PieceToken, which must be destroyed before the |PieceTracker|
  // instance that created it.
  std::unique_ptr<PieceToken> GetPieceToken(ObjectIdentifier identifier);

  // Returns the number of live tokens issued for |identifier|.
  int count(const ObjectIdentifier& identifier) const;

  // Returns the number of tracked identifiers.
  int size() const;

 private:
  // PieceToken implementation that increments and decrements the associated
  // counter in |token_counts_| during construction and destruction.
  class PieceTokenImpl : public PieceToken {
   public:
    explicit PieceTokenImpl(PieceTracker* tracker, ObjectIdentifier identifier);
    ~PieceTokenImpl() override;

    const ObjectIdentifier& GetIdentifier() const override;

   private:
    PieceTracker* tracker_;
    std::map<ObjectIdentifier, int>::iterator map_entry_;
  };

  // Number of live tokens per identifier. Entries are cleaned up when the count
  // reaches zero.
  std::map<ObjectIdentifier, int> token_counts_;
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
