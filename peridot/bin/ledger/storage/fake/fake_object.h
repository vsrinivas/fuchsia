// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_OBJECT_H_
#define PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_OBJECT_H_

#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/storage/public/object.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {
namespace fake {

class FakeObject : public Object {
 public:
  FakeObject(ObjectIdentifier identifier, fxl::StringView content);
  ~FakeObject() override;
  ObjectIdentifier GetIdentifier() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  ObjectIdentifier identifier_;
  std::string content_;
};

}  // namespace fake
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_OBJECT_H_
