// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace examples {

class MediaPlayerParams {
 public:
  MediaPlayerParams(const ftl::CommandLine& command_line);

  bool is_valid() const { return is_valid_; }

  const std::string& url() const { return url_; }

  const std::string& device_name() const { return device_name_; }

  const std::string& service_name() const { return service_name_; }

  bool stay() const { return stay_; }

 private:
  void Usage();

  bool is_valid_;

  std::string path_;
  std::string url_;
  std::string device_name_;
  std::string service_name_;
  bool stay_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerParams);
};

}  // namespace examples
