// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_MODES_H_
#define GARNET_BIN_IQUERY_MODES_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/inspect/reader.h>

#include <vector>

#include "garnet/bin/iquery/options.h"
#include "garnet/bin/iquery/utils.h"
#include "garnet/public/lib/inspect/query/source.h"

namespace iquery {

fit::promise<std::vector<inspect::Source>> RunCat(const Options*);
fit::promise<std::vector<inspect::Source>> RunFind(const Options*);
fit::promise<std::vector<inspect::Source>> RunLs(const Options*);

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_MODES_H_
