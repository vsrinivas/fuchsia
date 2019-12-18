// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_COMPARATOR_H_
#define TOOLS_FIDLCAT_LIB_COMPARATOR_H_

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace fidlcat {

class Comparator {
 public:
  Comparator(std::string_view compare_file_name) : compare_file_name_(compare_file_name) {}
  std::ostringstream& output_stream() { return output_stream_; }
  void Compare(std::ostream& os);

 private:
  const std::string compare_file_name_;
  std::ostringstream output_stream_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_COMPARATOR_H_
