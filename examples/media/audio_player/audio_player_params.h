// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace examples {

class AudioPlayerParams {
 public:
  AudioPlayerParams(const ftl::CommandLine& command_line);

  bool is_valid() const { return is_valid_; }

  const std::string& path() const { return path_; }

  const std::string& url() const { return url_; }

 private:
  void Usage();

  bool is_valid_;

  std::string path_;
  std::string url_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AudioPlayerParams);
};

}  // namespace examples
