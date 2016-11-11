// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/trace/configuration.h"

#include "lib/ftl/strings/split_string.h"
#include "lib/ftl/strings/string_number_conversions.h"

namespace tracing {

Configuration Configuration::ParseOrExit(const ftl::CommandLine& cl) {
  Configuration configuration;

  size_t index = 0;

  if (cl.HasOption("categories", &index)) {
    configuration.categories =
        ftl::SplitStringCopy(cl.options()[index].value, ",",
                             ftl::kTrimWhitespace, ftl::kSplitWantNonEmpty);
  }

  if (cl.HasOption("output-file", &index)) {
    configuration.output_file_name = cl.options()[index].value;
  }

  if (cl.HasOption("buffer-size", &index)) {
    size_t buffer_size = 2;
    if (!ftl::StringToNumberWithError(cl.options()[index].value,
                                      &buffer_size)) {
      FTL_LOG(ERROR) << "Failed to parse command-line option buffer-size: "
                     << cl.options()[index].value;
      exit(1);
    }
    configuration.buffer_size *= 1024 * 1024;
  }

  if (cl.HasOption("duration", &index)) {
    uint64_t duration = 10;
    if (!ftl::StringToNumberWithError(cl.options()[index].value, &duration)) {
      FTL_LOG(ERROR) << "Failed to parse command-line option duration: "
                     << cl.options()[index].value;
      exit(1);
    }
    configuration.duration = ftl::TimeDelta::FromSeconds(duration);
  }

  const auto& positional_args = cl.positional_args();
  if (!positional_args.empty()) {
    configuration.launch_info = modular::ApplicationLaunchInfo::New();
    configuration.launch_info->url = fidl::String::From(positional_args[0]);
    configuration.launch_info->arguments =
        fidl::Array<fidl::String>::From(std::vector<std::string>(
            positional_args.begin() + 1, positional_args.end()));
  }

  return configuration;
}

}  // namespace tracing
