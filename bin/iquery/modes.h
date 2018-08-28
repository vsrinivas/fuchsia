// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_MODES_H_
#define GARNET_BIN_IQUERY_MODES_H_

#include <vector>

#include "garnet/bin/iquery/options.h"
#include "garnet/bin/iquery/utils.h"

namespace iquery {

bool RunCat(const Options&, std::vector<ObjectNode>* out);
bool RunFind(const Options&, std::vector<ObjectNode>* out);
bool RunLs(const Options&, std::vector<ObjectNode>* out);

}  // namespace iquery

#endif
