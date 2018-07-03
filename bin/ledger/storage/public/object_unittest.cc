// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/public/object.h"

#include <lib/fsl/vmo/strings.h>

#include "gtest/gtest.h"

namespace storage {
namespace {

class StringObject : public Object {
 public:
  explicit StringObject(std::string value) : value_(std::move(value)) {}
  ~StringObject() override {}

  ObjectIdentifier GetIdentifier() const override { return {1u, 2u, "digest"}; }

  Status GetData(fxl::StringView* data) const override {
    *data = value_;
    return Status::OK;
  }

 private:
  std::string value_;
};

TEST(ObjectTest, GetVmo) {
  std::string content = "content";
  StringObject object(content);

  fsl::SizedVmo vmo;
  ASSERT_EQ(Status::OK, object.GetVmo(&vmo));
  std::string vmo_content;
  ASSERT_TRUE(fsl::StringFromVmo(vmo, &vmo_content));
  EXPECT_EQ(content, vmo_content);
}

}  // namespace
}  // namespace storage
