// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/components/echo/cpp/echo_component.h"

#include <numeric>

namespace echo {

static std::string join(std::vector<std::string>& input_list, const std::string& separator) {
  return std::accumulate(std::begin(input_list), std::end(input_list), std::string(""),
                         [&separator](std::string current, std::string& next) {
                           return current.empty() ? next : (std::move(current) + separator + next);
                         });
}

// Return a proper greeting for the list
std::string greeting(std::vector<std::string>& names) {
  // Join the list of names based on length
  auto number_of_names = names.size();
  switch (number_of_names) {
    case 0:
      return "Nobody!";
    case 1:
      return join(names, "");
    case 2:
      return join(names, " and ");
    default:
      return join(names, ", ");
  }
}

}  // namespace echo
