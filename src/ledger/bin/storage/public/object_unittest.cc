// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/public/object.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/vmo/strings.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace {

class StringObject : public Object {
 public:
  explicit StringObject(std::string value) : value_(std::move(value)) {}
  ~StringObject() override = default;

  ObjectIdentifier GetIdentifier() const override {
    return ObjectIdentifier(1u, ObjectDigest("digest"), nullptr);
  }

  Status GetData(absl::string_view* data) const override {
    *data = value_;
    return Status::OK;
  }

  Status AppendReferences(ObjectReferencesAndPriority* references) const override {
    return Status::OK;
  }

 private:
  std::string value_;
};

TEST(ObjectTest, GetVmo) {
  std::string content = "content";
  StringObject object(content);

  ledger::SizedVmo vmo;
  ASSERT_EQ(object.GetVmo(&vmo), Status::OK);
  std::string vmo_content;
  ASSERT_TRUE(ledger::StringFromVmo(vmo, &vmo_content));
  EXPECT_EQ(vmo_content, content);
}

}  // namespace
}  // namespace storage
