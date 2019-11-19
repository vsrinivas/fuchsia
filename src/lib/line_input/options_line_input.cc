// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/lib/line_input/options_line_input.h"

#include "lib/fit/defer.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace line_input {

void OptionsLineInputBase::PromptOptions(const std::vector<std::string>& options,
                                         OptionsCallback callback) {
  options_ = std::move(options);
  callback_ = std::move(callback);
}

void OptionsLineInputBase::HandleLine(const std::string& line, bool canceled) {
  FXL_DCHECK(is_active());
  fit::result<void, std::string> result = fit::ok();
  std::vector<int> chosen_options;
  auto defer_callback =
      fit::defer([&result, &chosen_options, callback = std::move(callback_)]() mutable {
        callback(std::move(result), chosen_options);
      });

  if (canceled) {
    result = fit::error("");
    return;
  }

  auto options = fxl::SplitString(line, " ", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  if (options.empty()) {
    result = fit::error("No options given.");
    return;
  }

  // All entries should be indices.
  std::vector<int> indices;
  indices.reserve(options.size());
  for (const auto& option : options) {
    int index = -1;
    if (!fxl::StringToNumberWithError(option, &index) ||
        (index <= 0 || index > static_cast<int>(options_.size()))) {
      result = fit::error(fxl::StringPrintf("Invalid index %s", option.ToString().c_str()));
      return;
    }

    indices.push_back(index - 1);
  }

  // Fill in the chosen options.
  chosen_options = std::move(indices);
}

// OptionsLineInputStdout --------------------------------------------------------------------------

OptionsLineInputStdout::OptionsLineInputStdout(const std::string& prompt)
    : LineInputStdout([this](const std::string& s) { HandleLine(s, IsEof()); }, prompt) {}

}  // namespace line_input
