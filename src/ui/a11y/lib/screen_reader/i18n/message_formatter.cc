// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/i18n/message_formatter.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/ui/a11y/lib/screen_reader/i18n/icu_headers.h"

namespace a11y {
namespace i18n {

MessageFormatter::MessageFormatter(icu::Locale locale, std::unique_ptr<intl::Lookup> lookup)
    : locale_(std::move(locale)), lookup_(std::move(lookup)) {}

std::optional<std::string> MessageFormatter::FormatStringById(
    const uint64_t id, const std::vector<std::string>& arg_names,
    const std::vector<ArgValue>& arg_values) {
  auto lookup_result = lookup_->String(id);
  if (lookup_result.is_error()) {
    FX_LOGS(ERROR) << "Failed to retrieve the message with ID == " << id
                   << " lookup error code == " << static_cast<int>(lookup_result.error());
    return std::nullopt;
  }
  std::string_view message_pattern = lookup_result.value();

  if (arg_names.size() != arg_values.size()) {
    FX_LOGS(ERROR) << "Different number of values than value names";
    return std::nullopt;
  }

  if (arg_values.empty()) {
    // There is not formatting to be done, so simplily returns the message.
    return std::string(message_pattern);
  }

  UErrorCode status = U_ZERO_ERROR;
  icu::MessageFormat message_format(std::string(message_pattern).c_str(), locale_, status);
  if (U_FAILURE(status)) {
    FX_LOGS(ERROR) << "Tried to build an icu::MessageFormat with a invalid string pattern: "
                   << message_pattern;
    return std::nullopt;
  }

  std::vector<icu::Formattable> icu_arg_values;
  for (auto& value : arg_values) {
    switch (value.index()) {
      case 0:
        icu_arg_values.emplace_back(std::get<std::string>(value).c_str());
        break;
      case 1:
        icu_arg_values.emplace_back(std::get<int64_t>(value));
        break;
    }
  }
  std::vector<icu::UnicodeString> icu_arg_names;
  for (auto& name : arg_names) {
    icu_arg_names.emplace_back(name.c_str());
  }

  // checks if the names of the arguments being passed exist in the pattern.
  status = U_ZERO_ERROR;
  std::unique_ptr<icu::StringEnumeration> format_names(message_format.getFormatNames(status));
  if (U_FAILURE(status)) {
    FX_LOGS(ERROR) << "Couldn't get format names for the icu::MessageFormat";
    return std::nullopt;
  }
  status = U_ZERO_ERROR;
  if (static_cast<int32_t>(icu_arg_names.size()) != format_names->count(status)) {
    FX_DCHECK(U_SUCCESS(status))
        << "Wrong number of arguments provided for the given icu::MessageFormat";
    return std::nullopt;
  }
  for (const auto& name : icu_arg_names) {
    status = U_ZERO_ERROR;
    auto* pattern_name = format_names->snext(status);
    FX_DCHECK(U_SUCCESS(status)) << "Failed to retrieve the next format name in the pattern.";
    FX_DCHECK(pattern_name != nullptr);
    if (name != *pattern_name) {
      return std::nullopt;
    }
  }

  icu::UnicodeString unicode_result;
  status = U_ZERO_ERROR;
  message_format.format(icu_arg_names.data(), icu_arg_values.data(), icu_arg_values.size(),
                        unicode_result, status);
  if (U_FAILURE(status)) {
    FX_LOGS(ERROR) << "Failed to format message";
    return std::nullopt;
  }

  std::string utf8_output;
  unicode_result.toUTF8String(utf8_output);
  return utf8_output;
}

}  // namespace i18n
}  // namespace a11y
