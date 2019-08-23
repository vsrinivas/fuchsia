// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_GPRETTY_PRINTERS_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_GPRETTY_PRINTERS_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/fostr/fidl/fuchsia/mem/formatting.h>
#include <lib/fostr/indent.h>
#include <lib/fsl/vmo/strings.h>

#include <ostream>

namespace fit {

// Pretty-prints fit::result_state in gTest matchers instead of the default byte string in case of
// failed expectations.
void PrintTo(const fit::result_state& state, std::ostream* os) {
  std::string state_str;
  switch (state) {
    case fit::result_state::pending:
      state_str = "PENDING";
      break;
    case fit::result_state::ok:
      state_str = "OK";
      break;
    case fit::result_state::error:
      state_str = "ERROR";
      break;
  }
  *os << state_str;
}

}  // namespace fit

namespace fuchsia {
namespace feedback {

// Pretty-prints Attachment in gTest matchers instead of the default byte string in case of failed
// expectations.
void PrintTo(const Attachment& attachment, std::ostream* os) {
  *os << fostr::Indent;
  *os << fostr::NewLine << "key: " << attachment.key;
  *os << fostr::NewLine << "value: ";
  std::string value;
  if (fsl::StringFromVmo(attachment.value, &value)) {
    if (value.size() < 1024) {
      *os << "'" << value << "'";
    } else {
      *os << "(string too long)" << attachment.value;
    }
  } else {
    *os << attachment.value;
  }
  *os << fostr::Outdent;
}

}  // namespace feedback

namespace mem {

// Pretty-prints string VMOs in gTest matchers instead of the default byte string in case of failed
// expectations.
void PrintTo(const Buffer& vmo, std::ostream* os) {
  std::string value;
  FXL_CHECK(fsl::StringFromVmo(vmo, &value));
  *os << "'" << value << "'";
}

}  // namespace mem
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_GPRETTY_PRINTERS_H_
