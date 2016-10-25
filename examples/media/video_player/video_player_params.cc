// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/video_player/video_player_params.h"

#include "lib/ftl/strings/split_string.h"

namespace examples {

VideoPlayerParams::VideoPlayerParams(const std::string& connection_url) {
  // TODO(dalesat): Use a real URL parser.
  is_valid_ = false;

  size_t query_index = connection_url.find('?');
  if (query_index == std::string::npos) {
    FTL_LOG(ERROR) << "URL must have query, e.g. mojo:video_player?path=<path>";
    return;
  }

  std::string query = connection_url.substr(query_index + 1);

  for (const auto& param : ftl::SplitStringCopy(
           query, "&", ftl::kKeepWhitespace, ftl::kSplitWantNonEmpty)) {
    size_t equal_index = param.find('=');

    if (equal_index == std::string::npos) {
      if (param == "path") {
        FTL_LOG(ERROR) << "Parameter 'path' must have a value";
        return;
      } else {
        FTL_LOG(ERROR) << "Unrecognized query parameter '" << param << "'";
        return;
      }
    } else {
      std::string key = param.substr(0, equal_index);
      std::string value = param.substr(equal_index + 1);

      if (value.empty()) {
        FTL_LOG(ERROR) << "Value expected after '=' in parameter '" << key
                       << "'";
        return;
      }

      if (key == "path") {
        path_ = value;
      } else {
        FTL_LOG(ERROR) << "Unrecognized query parameter '" << key << "'";
        return;
      }
    }
  }

  is_valid_ = true;
}

}  // namespace examples
