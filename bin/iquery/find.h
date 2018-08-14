// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_FIND_H_
#define GARNET_BIN_IQUERY_FIND_H_

#include <string>
#include <vector>

namespace iquery {

bool FindObjects(const std::string& base_directory,
                 std::vector<std::string>* out_results);

}

#endif  // GARNET_BIN_IQUERY_FIND_H_
