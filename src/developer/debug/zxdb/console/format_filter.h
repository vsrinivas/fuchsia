// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_FILTER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_FILTER_H_

#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class ConsoleContext;
class Filter;
class Job;

OutputBuffer FormatFilter(const ConsoleContext* context, const Filter* filter);

// Formats the current filter list in a table. If |for_job| is provided, only those filters
// matching the given job will be output. Otherwise, all filters will be output.
OutputBuffer FormatFilterList(ConsoleContext* context, const Job* for_job = nullptr,
                              int indent = 0);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_FILTER_H_
