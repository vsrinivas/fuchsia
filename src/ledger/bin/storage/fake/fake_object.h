// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_H_
#define SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_H_

#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/strings/string_view.h"

namespace storage {
namespace fake {

class FakePiece : public Piece {
 public:
  FakePiece(ObjectIdentifier identifier, fxl::StringView content);

  fxl::StringView GetData() const override;
  ObjectIdentifier GetIdentifier() const override;
  Status AppendReferences(
      ObjectReferencesAndPriority* references) const override;

 private:
  ObjectIdentifier identifier_;
  std::string content_;
};

class FakeObject : public Object {
 public:
  explicit FakeObject(std::unique_ptr<const Piece> piece);
  explicit FakeObject(ObjectIdentifier identifier, fxl::StringView content);

  ObjectIdentifier GetIdentifier() const override;
  Status GetData(fxl::StringView* data) const override;
  Status AppendReferences(
      ObjectReferencesAndPriority* references) const override;

 private:
  std::unique_ptr<const Piece> piece_;
};

class FakeTokenChecker;

class FakeObjectToken : public ObjectToken {
 public:
  explicit FakeObjectToken(ObjectIdentifier identifier);

  // Returns a token checker associated with this token.
  FakeTokenChecker GetChecker();

  // ObjectToken:
  const ObjectIdentifier& GetIdentifier() const override;

 private:
  ObjectIdentifier identifier_;
  fxl::WeakPtrFactory<FakeObjectToken> weak_factory_;
};

// This class allows to decide if a particular FakeObjectToken is still alive.
class FakeTokenChecker {
 public:
  explicit FakeTokenChecker(const fxl::WeakPtr<FakeObjectToken>& token);

  // The token checker converts to true iff the ObjectToken is still alive.
  explicit operator bool() const;

 private:
  fxl::WeakPtr<FakeObjectToken> token_;
};

}  // namespace fake
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_H_
