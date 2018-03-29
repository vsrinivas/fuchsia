// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_HANDLER_RECORD_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_HANDLER_RECORD_H_

#include <fuchsia/cpp/modular.h>

namespace modular {

struct QueryHandlerRecord {
  QueryHandlerRecord(QueryHandlerPtr handler, std::string url)
      : handler(std::move(handler)), url(std::move(url)) {}

  QueryHandlerPtr handler;
  std::string url;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_HANDLER_RECORD_H_
