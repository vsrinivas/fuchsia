// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_FORMATTERS_JSON_H_
#define GARNET_BIN_IQUERY_FORMATTERS_JSON_H_

#include "garnet/bin/iquery/formatter.h"

namespace iquery {

class JsonFormatter : public Formatter {
 public:
  std::string Format(const Options&,
                     const std::vector<inspect::ObjectSource>&) override;
};

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_FORMATTERS_JSON_H_
