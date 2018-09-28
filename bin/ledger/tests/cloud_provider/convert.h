// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTS_CLOUD_PROVIDER_CONVERT_H_
#define PERIDOT_BIN_LEDGER_TESTS_CLOUD_PROVIDER_CONVERT_H_

#include <string>

#include <lib/fidl/cpp/vector.h>

namespace cloud_provider {

fidl::VectorPtr<uint8_t> ToArray(const std::string& val);

std::string ToString(const fidl::VectorPtr<uint8_t>& bytes);

std::string ToHex(const std::string& bytes);

}  // namespace cloud_provider

#endif  // PERIDOT_BIN_LEDGER_TESTS_CLOUD_PROVIDER_CONVERT_H_
