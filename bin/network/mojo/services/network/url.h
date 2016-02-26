// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_URL_H_
#define MOJO_SERVICES_NETWORK_URL_H_

#include <string>

namespace mojo {

class URL {
 public:
  URL(const std::string& str);
  ~URL();

  bool IsParsed();

  std::string& Proto();
  std::string& Host();
  std::string& Port();
  std::string& Path();

 private:
  const std::string str_;

  bool Parse();

  bool parsed_;
  std::string proto_;
  std::string host_;
  std::string port_;
  std::string path_;
};

}  // namespace mojo

#endif  // MOJO_SERVICES_NETWORK_URL_H_
