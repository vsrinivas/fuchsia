// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_H_
#define SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_H_

#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace fake {

class FakePiece : public Piece {
 public:
  FakePiece(ObjectIdentifier identifier, absl::string_view content);

  absl::string_view GetData() const override;
  ObjectIdentifier GetIdentifier() const override;
  Status AppendReferences(ObjectReferencesAndPriority* references) const override;

  std::unique_ptr<const FakePiece> Clone() const;

 private:
  ObjectIdentifier identifier_;
  std::string content_;
};

class FakeObject : public Object {
 public:
  explicit FakeObject(std::unique_ptr<const Piece> piece);
  explicit FakeObject(ObjectIdentifier identifier, absl::string_view content);

  ObjectIdentifier GetIdentifier() const override;
  Status GetData(absl::string_view* data) const override;
  Status AppendReferences(ObjectReferencesAndPriority* references) const override;

 private:
  std::unique_ptr<const Piece> piece_;
};

}  // namespace fake
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_OBJECT_H_
