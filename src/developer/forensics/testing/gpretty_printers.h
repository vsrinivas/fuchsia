// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_GPRETTY_PRINTERS_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_GPRETTY_PRINTERS_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>

#include <ostream>

#include <src/lib/fostr/fidl/fuchsia/mem/formatting.h>
#include <src/lib/fostr/indent.h>

#include "src/developer/forensics/crash_reports/item_location.h"
#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/fsl/vmo/strings.h"

namespace fit {

// Pretty-prints fpromise::result_state in gTest matchers instead of the default byte string in case
// of failed expectations.
inline void PrintTo(const fpromise::result_state& state, std::ostream* os) {
  std::string state_str;
  switch (state) {
    case fpromise::result_state::pending:
      state_str = "PENDING";
      break;
    case fpromise::result_state::ok:
      state_str = "OK";
      break;
    case fpromise::result_state::error:
      state_str = "ERROR";
      break;
  }
  *os << state_str;
}

}  // namespace fit

namespace forensics {

inline void PrintTo(const Error error, std::ostream* os) { *os << ToString(error); }

template <typename T>
inline void PrintTo(const ErrorOr<T>& error_or, std::ostream* os) {
  if (error_or.HasValue()) {
    *os << error_or.Value();
  } else {
    *os << ToString(error_or.Error());
  }
}

namespace crash_reports {

// Pretty-prints ItemLocation in gTest matchers instead of the default byte string in case
// of failed expectations.
inline void PrintTo(const ItemLocation& location, std::ostream* os) {
  std::string location_str;
  switch (location) {
    case ItemLocation::kMemory:
      location_str = "MEMORY";
      break;
    case ItemLocation::kCache:
      location_str = "CACHE";
      break;
    case ItemLocation::kTmp:
      location_str = "TMP";
      break;
  }
  *os << location_str;
}

}  // namespace crash_reports

namespace feedback {

namespace pretty {

// Display ASCII character as is or non-ascii characters by their {hex value}.
inline void Format(const char ch, std::stringstream* output) {
  if (ch == '\n' || ch == '\t') {
    *output << ch;
  } else if (ch >= ' ' && ch <= '~') {
    *output << ch;
  } else {
    *output << "{0x" << ((int)ch & 0xFF) << "}";
  }
}

// Removes exception "FormatException: Unexpected extension byte" when calling PrintTo() with
// ostream = std::cout by converting all non-ascii character to "{hex_value}".
inline std::string Format(const std::string& input) {
  return input;
  std::stringstream output;
  // Display HEX code for integer-cast non-ascii characters.
  output << std::hex;
  for (char ch : input) {
    Format(ch, &output);
  }
  return output.str();
}

}  // namespace pretty

inline void PrintTo(const AttachmentValue& value, std::ostream* os) {
  *os << fostr::Indent;
  *os << "{ ";
  switch (value.State()) {
    case AttachmentValue::State::kComplete:
      *os << "VALUE : " << pretty::Format(value.Value());
      break;
    case AttachmentValue::State::kPartial:
      *os << "VALUE : " << pretty::Format(value.Value());
      *os << ", ERROR : " << ToString(value.Error());
      break;
    case AttachmentValue::State::kMissing:
      *os << "ERROR : " << ToString(value.Error());
      break;
  }
  *os << " }";
  *os << fostr::Outdent;
}

}  // namespace feedback

}  // namespace forensics

namespace fuchsia {
namespace feedback {

// Pretty-prints Attachment in gTest matchers instead of the default byte string in case of failed
// expectations.
inline void PrintTo(const Attachment& attachment, std::ostream* os) {
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

// Pretty-prints Annotation in gTest matchers instead of the default byte string in case of failed
// expectations.
inline void PrintTo(const Annotation& annotation, std::ostream* os) {
  *os << fostr::Indent;
  *os << fostr::NewLine << "key: " << annotation.key;
  *os << fostr::NewLine << "value: ";
  *os << fostr::NewLine << "value: " << annotation.value;
  *os << fostr::Outdent;
}

}  // namespace feedback

namespace mem {

// Pretty-prints string VMOs in gTest matchers instead of the default byte string in case of failed
// expectations.
inline void PrintTo(const Buffer& vmo, std::ostream* os) {
  std::string value;
  FX_CHECK(fsl::StringFromVmo(vmo, &value));
  *os << "'" << value << "'";
}

}  // namespace mem
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_GPRETTY_PRINTERS_H_
