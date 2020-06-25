// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/i18n/tests/mocks/mock_message_formatter.h"

namespace accessibility_test {

MockMessageFormatter::MockMessageFormatter() : a11y::i18n::MessageFormatter() {}

std::optional<std::string> MockMessageFormatter::FormatStringById(
    const uint64_t id, const std::vector<std::string>& arg_names,
    const std::vector<std::string>& arg_values) {
  auto it = id_to_message_.find(id);
  if (it == id_to_message_.end()) {
    return std::nullopt;
  }

  return it->second;
}

void MockMessageFormatter::SetMessageForId(const uint64_t id, std::string message) {
  id_to_message_[id] = std::move(message);
}

}  // namespace accessibility_test
