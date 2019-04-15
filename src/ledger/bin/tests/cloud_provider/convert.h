// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_CONVERT_H_
#define SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_CONVERT_H_

#include <lib/fidl/cpp/vector.h>

#include <string>

namespace cloud_provider {

fidl::VectorPtr<uint8_t> ToArray(const std::string& val);

std::string ToString(const fidl::VectorPtr<uint8_t>& bytes);

std::string ToHex(const std::string& bytes);

}  // namespace cloud_provider

#endif  // SRC_LEDGER_BIN_TESTS_CLOUD_PROVIDER_CONVERT_H_
