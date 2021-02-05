// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START includes]
#include <iostream>
#include <memory>

// This header file has been generated from the strings library fuchsia.intl.l10n.
#include "fuchsia/intl/l10n/cpp/fidl.h"
#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/intl/lookup/cpp/lookup.h"
// [END includes]

// [START generated]
// Each library name segment between dots gets its own nested namespace in
// the generated C++ code.
using fuchsia::intl::l10n::MessageIds;
using intl::Lookup;
// [END generated]

// [START usage_example]
int main() {
  static const uint64_t MESSAGE_ID = static_cast<uint64_t>(MessageIds::STRING_NAME);
  std::cout << "Message ID: " << MESSAGE_ID << std::endl;

  // "es" is the most preferred locale. "en-US" will be used if "es" is unavailable.  In addition,
  // "en-US" automatically falls back to "en".
  auto result = Lookup::New({"es", "en-US"});
  FX_CHECK(result.is_ok()) << "Could not load lookup. Status: "
                           << static_cast<int8_t>(result.error());

  const std::unique_ptr<Lookup> lookup = result.take_value();
  auto translation = lookup->String(MESSAGE_ID);
  FX_CHECK(translation.is_ok()) << "Could not load a message translation"
                                << static_cast<int8_t>(result.error());

  std::cout << "Translated message: " << translation.value();
  return 0;
}
// [END usage_example]
