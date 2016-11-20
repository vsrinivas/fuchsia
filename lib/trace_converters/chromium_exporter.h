// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_CONVERTERS_CHROMIUM_EXPORTER_H_
#define APPS_TRACING_LIB_TRACE_CONVERTERS_CHROMIUM_EXPORTER_H_

#include <fstream>

#include "apps/tracing/lib/trace/reader.h"
#include "third_party/rapidjson/rapidjson/ostreamwrapper.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace tracing {

class ChromiumExporter {
 public:
  explicit ChromiumExporter(std::ofstream file_out);
  explicit ChromiumExporter(std::ostream& out);
  ~ChromiumExporter();

  void ExportRecord(const reader::Record& record);

 private:
  void Start();
  void ExportEvent(const reader::Record::Event& event);

  std::ofstream file_out_;
  rapidjson::OStreamWrapper wrapper_;
  rapidjson::Writer<rapidjson::OStreamWrapper> writer_;
};

}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_CONVERTERS_CHROMIUM_EXPORTER_H_
