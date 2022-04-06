// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_COMPONENTS_ECHO_CPP_ECHO_COMPONENT_H_
#define EXAMPLES_COMPONENTS_ECHO_CPP_ECHO_COMPONENT_H_

// [START greeting]
#include <string>
#include <vector>

namespace echo {

std::string greeting(std::vector<std::string>& names);

}  // namespace echo
// [END greeting]

#endif  // EXAMPLES_COMPONENTS_ECHO_CPP_ECHO_COMPONENT_H_
