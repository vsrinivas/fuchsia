// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trace_converters/chromium_exporter.h"

#include <fbl/vector.h>
#include <gtest/gtest.h>
#include <trace-reader/records.h>

#include <sstream>

namespace {

TEST(ChromiumExporterTest, ValidUtf8) {
  trace::EventData data(trace::EventData::Instant{trace::EventScope::kGlobal});
  fbl::Vector<trace::Argument> arguments;
  arguments.push_back(
      trace::Argument("arg", trace::ArgumentValue::MakeString("foo\xb5\xb3")));
  trace::Record record(trace::Record::Event{
      1000, trace::ProcessThread(45, 46), "c\342\202at", "n\301a\205me",
      std::move(arguments), std::move(data)});

  std::ostringstream out_stream;

  tracing::ChromiumExporter exporter(out_stream);
  exporter.ExportRecord(record);

  EXPECT_EQ(
      out_stream.str(),
      "{\"displayTimeUnit\":\"ns\",\"traceEvents\":[{\"cat\":\"c\uFFFDat\","
      "\"name\":\"n\uFFFDa\uFFFDme\",\"ts\":1.0,\"pid\":45,\"tid\":46,\"ph\":"
      "\"i\",\"s\":\"g\",\"args\":{\"arg\":\"foo\uFFFD\uFFFD\"}}");
}

}  // namespace
