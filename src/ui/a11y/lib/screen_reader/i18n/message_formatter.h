// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_I18N_MESSAGE_FORMATTER_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_I18N_MESSAGE_FORMATTER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/lib/intl/lookup/cpp/lookup.h"
#include "third_party/icu/source/i18n/unicode/msgfmt.h"

namespace a11y {
namespace i18n {

// A MessageFormatter provides an API for callers to provide a message ID along with optional
// argument names and values, and receive a formatted message back. This class does not load the
// messages itself. It queries the |lookup_| interface, which is received in the constructor. To
// obtain messages localized in a specific locale, callers must first load the desired resources
// through |lookup_|, and then access  them through this class via their message IDs. For this
// class, message IDs point to a valid icu::MessageFormat pattern in |lookup_|. The language
// parameter in the constructor affects how certain icu::MessageFormat patterns are formatted,
// matching what is expected in that language. More info can be found in the icu::MessageFormat
// documentation.
class MessageFormatter {
 public:
  // |lookup_| must be initialized and holds the icu::MessageFormat patterns.
  explicit MessageFormatter(icu::Locale locale, std::unique_ptr<intl::Lookup> lookup);
  virtual ~MessageFormatter() = default;

  // Formats a icu::MessageFormat pattern pointed by |id|, optionally using |arg_names| which map to
  // |arg_values|. Returns the formatted string, in UTF-8 when successful. If it fails,
  // std::nullopt. The number or |arg_names| must match the number of |arg_values|. If one of
  // |arg_names| does not exist in the pattern, the formatting returns an error. For now, no complex
  // error codes are needed, so std::optional is used. If necessary, it is an easy change to
  // introduce custom error codes.
  virtual std::optional<std::string> FormatStringById(
      const uint64_t id, const std::vector<std::string>& arg_names = std::vector<std::string>(),
      const std::vector<std::string>& arg_values = std::vector<std::string>());

  // Returns the icu locale used to format messages.
  const icu::Locale& locale() const { return locale_; }

 protected:
  // For mocks.
  MessageFormatter() = default;

 private:
  // Locale used to build MessageFormat.
  icu::Locale locale_;
  // API where messages are retrieeved.
  std::unique_ptr<intl::Lookup> lookup_;
};

}  // namespace i18n
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_I18N_MESSAGE_FORMATTER_H_
