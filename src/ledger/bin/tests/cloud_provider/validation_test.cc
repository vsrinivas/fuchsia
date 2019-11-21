// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/cloud_provider/validation_test.h"

#include "peridot/lib/base64url/base64url.h"
#include "src/ledger/lib/convert/convert.h"

namespace cloud_provider {

ValidationTest::ValidationTest()
    : component_context_(sys::ComponentContext::Create()), random_(test_loop().initial_state()) {}

ValidationTest::~ValidationTest() = default;

std::vector<uint8_t> ValidationTest::GetUniqueRandomId() {
  return convert::ToArray(base64url::Base64UrlEncode(random_.RandomUniqueBytes()));
}

void ValidationTest::SetUp() { component_context_->svc()->Connect(cloud_provider_.NewRequest()); }

}  // namespace cloud_provider
