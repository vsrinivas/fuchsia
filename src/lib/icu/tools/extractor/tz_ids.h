// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ICU_TOOLS_EXTRACTOR_TZ_IDS_H_
#define SRC_LIB_ICU_TOOLS_EXTRACTOR_TZ_IDS_H_

#include "command.h"

namespace icu_data_extractor {

// Command: "tz-ids"
//
// Extracts a list of time zone IDs from the loaded ICU data and writes it to the output path, if
// given, or to STDOUT otherwise.
class TzIds : public icu_data_extractor::Command {
  std::string_view Name() const final;
  int Execute(const fxl::CommandLine& command_line,
              const fxl::CommandLine& sub_command_line) const final;
  void PrintDocs(std::ostream& os) const final;
};

}  // namespace icu_data_extractor

#endif  // SRC_LIB_ICU_TOOLS_EXTRACTOR_TZ_IDS_H_
