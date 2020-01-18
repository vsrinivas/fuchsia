// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tz_ids.h"

#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "common.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "third_party/icu/source/common/unicode/strenum.h"
#include "third_party/icu/source/common/unicode/utypes.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace icu_data_extractor {

namespace {

constexpr const char kName[] = "tz-ids";

constexpr const char kArgFixedOrder[] = "fixed-order";
constexpr const char kArgDelimiter[] = "delimiter";

constexpr const char kDefaultDelimiter[] = "\n";

std::vector<std::string> GetFixedOrderIds(const fxl::CommandLine& sub_command_line) {
  if (sub_command_line.HasOption(kArgFixedOrder)) {
    std::string items_str;
    sub_command_line.GetOptionValue(kArgFixedOrder, &items_str);
    return fxl::SplitStringCopy(items_str, ",", fxl::WhiteSpaceHandling::kTrimWhitespace,
                                fxl::SplitResult::kSplitWantNonEmpty);
  }
  return std::vector<std::string>{};
}

}  // namespace

std::string_view TzIds::Name() const { return kName; }

int TzIds::Execute(const fxl::CommandLine& command_line,
                   const fxl::CommandLine& sub_command_line) const {
  const auto output_path = GetOutputPath(sub_command_line);

  const auto fixed_order_ids = GetFixedOrderIds(sub_command_line);
  std::set<std::string> fixed_set(fixed_order_ids.begin(), fixed_order_ids.end());

  UErrorCode status = UErrorCode::U_ZERO_ERROR;

  auto ids = icu::TimeZone::createEnumeration();
  auto count = ids->count(status);

  if (U_FAILURE(status)) {
    std::cerr << "Error: " << u_errorName(status) << std::endl;
    return -1;
  }

  std::vector<std::string> reordered_ids;
  reordered_ids.reserve(count);

  reordered_ids.insert(reordered_ids.end(), fixed_order_ids.begin(), fixed_order_ids.end());

  for (const char* id = nullptr; (id = ids->next(nullptr, status)) != nullptr;) {
    const std::string id_str(id);
    if (fixed_set.erase(id_str) == 0) {
      reordered_ids.push_back(id_str);
    }
  }

  if (!fixed_set.empty()) {
    std::cerr << "Fixed order IDs not found in ICU data: ";
    for (const auto& id : fixed_set) {
      std::cerr << id << " ";
    }
    std::cerr << std::endl;
    return -1;
  }

  const std::string delimiter =
      sub_command_line.GetOptionValueWithDefault(kArgDelimiter, kDefaultDelimiter);
  const auto output = fxl::JoinStrings(reordered_ids, delimiter);

  return WriteToOutputFileOrStdOut(sub_command_line, output);
}

void TzIds::PrintDocs(std::ostream& os) const {
  os << "  " << kName << "\n    --" << kArgOutputPath
     << "=FILE\t\t\tPath to output file (if omitted, STDOUT)"
     << "\n    --" << kArgFixedOrder << "=ID1,ID2,...\t\tList of time zone IDs to put at the top"
     << "\n    --" << kArgDelimiter
     << "=DELIMITER\t\tOptional delimiter to insert between IDs (default: \"\\n\")"
     << "\n\n  Extract a list of time zone IDs";
}

}  // namespace icu_data_extractor
