// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"

#include "mojo/services/network/url.h"

#include <algorithm>

namespace mojo {

URL::URL(const std::string& str) : str_(str) {
  parsed_ = Parse();
}

URL::~URL() {}

bool URL::Parse() {
  std::string delim("://");
  std::string::const_iterator proto_end =
    std::search(str_.begin(), str_.end(), delim.begin(), delim.end());
  if (proto_end == str_.end()) {
    return false;
  }
  proto_.assign(str_.begin(), proto_end);

  std::string::const_iterator host_start = proto_end + delim.length();
  std::string::const_iterator path_start = std::find(host_start, str_.end(),
                                                     '/');
  std::string::const_iterator host_end = std::find(host_start, path_start,
                                                   ':');
  host_.assign(host_start, host_end);

  if (host_end != path_start)
    port_.assign(host_end + 1, path_start);
  else
    port_ = proto_;

  if (path_start != str_.end())
    path_.assign(path_start, str_.end());
  else
    path_.assign("/");

  if (proto_.length() == 0 || host_.length() == 0 || port_.length() == 0 ||
      path_.length() == 0)
    return false;

  return true;
}

bool URL::IsParsed() {
  return parsed_;
}

std::string& URL::Proto() {
  DCHECK(parsed_);
  return proto_;
}

std::string& URL::Host() {
  DCHECK(parsed_);
  return host_;
}

std::string& URL::Port() {
  DCHECK(parsed_);
  return port_;
}

std::string& URL::Path() {
  DCHECK(parsed_);
  return path_;
}

} // namespace mojo
