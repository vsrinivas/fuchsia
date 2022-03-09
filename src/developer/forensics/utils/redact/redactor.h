// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_REDACT_REDACTOR_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_REDACT_REDACTOR_H_

#include <string>
#include <string_view>
#include <vector>

#include "src/developer/forensics/utils/redact/cache.h"
#include "src/developer/forensics/utils/redact/replacer.h"

namespace forensics {

// TODO(fxbug.dev/94086): keep this class in sync with the Rust redactor until its deleted (located
// in
// https://osscs.corp.google.com/fuchsia/fuchsia/+/main:src/diagnostics/archivist/src/logs/redact.rs
class Redactor {
 public:
  Redactor();

  // Redacts |text| in-place and returns a reference to |text|.
  std::string& Redact(std::string& text);

  // Unredacted / redacted version of canary message for confirming log redaction.
  static std::string UnredactedCanary();
  static std::string RedactedCanary();

 private:
  Redactor& Add(Replacer replacer);
  Redactor& AddTextReplacer(std::string_view pattern, std::string_view replacement);
  Redactor& AddIdReplacer(std::string_view pattern, std::string_view format);

  RedactionIdCache cache_;
  std::vector<Replacer> replacers_;
};

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_REDACT_REDACTOR_H_
