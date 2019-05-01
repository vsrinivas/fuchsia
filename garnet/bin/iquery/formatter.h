// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_FORMATTER_H_
#define GARNET_BIN_IQUERY_FORMATTER_H_

#include <lib/inspect/query/source.h>
#include <lib/inspect/reader.h>

#include <string>
#include <vector>

#include "garnet/bin/iquery/modes.h"
#include "garnet/bin/iquery/options.h"
#include "garnet/bin/iquery/utils.h"

namespace iquery {

class Formatter {
 public:
  virtual ~Formatter() = default;
  virtual std::string Format(const Options&,
                             const std::vector<inspect::Source>&) = 0;
};

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_FORMATTER_H_
