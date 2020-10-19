// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trace_converters/chromium_exporter.h"

#include <sstream>

#include <fbl/vector.h>
#include <gtest/gtest.h>
#include <trace-reader/records.h>

namespace {

TEST(ChromiumExporterTest, ValidUtf8) {
  trace::EventData data(trace::EventData::Instant{trace::EventScope::kGlobal});
  fbl::Vector<trace::Argument> arguments;
  arguments.push_back(trace::Argument("arg", trace::ArgumentValue::MakeString("foo\xb5\xb3")));
  trace::Record record(trace::Record::Event{1000, trace::ProcessThread(45, 46), "c\342\202at",
                                            "n\301a\205me", std::move(arguments), std::move(data)});

  std::ostringstream out_stream;

  // Enclosing the exporter in its own scope ensures that its
  // cleanup routines are called by the destructor before the
  // output stream is read. This way, we can obtain the full
  // output rather than a truncated version.
  {
    tracing::ChromiumExporter exporter(out_stream);
    exporter.ExportRecord(record);
  }

  EXPECT_EQ(out_stream.str(),
            "{\"displayTimeUnit\":\"ns\",\"traceEvents\":[{\"cat\":\"c\uFFFDat\","
            "\"name\":\"n\uFFFDa\uFFFDme\",\"ts\":1.0,\"pid\":45,\"tid\":46,\"ph\":"
            "\"i\",\"s\":\"g\",\"args\":{\"arg\":\"foo\uFFFD\uFFFD\"}}"
            "],\"systemTraceEvents\":{\"type\":\"fuchsia\",\"events\":[]}}");
}

TEST(ChromiumExporterTest, UnknownLargeBlobEventDropped) {
  fbl::Vector<trace::Argument> arguments;
  arguments.push_back(trace::Argument("arg", trace::ArgumentValue::MakeString("foo")));
  static const char blob[] = "some test blob data";
  trace::Record record(trace::LargeRecordData{trace::LargeRecordData::BlobEvent{
      "category",
      "no::UnknownName",
      1000,
      trace::ProcessThread(45, 46),
      std::move(arguments),
      blob,
      sizeof(blob),
  }});

  std::ostringstream out_stream;

  // Enclosing the exporter in its own scope ensures that its
  // cleanup routines are called by the destructor before the
  // output stream is read. This way, we can obtain the full
  // output rather than a truncated version.
  {
    tracing::ChromiumExporter exporter(out_stream);
    exporter.ExportRecord(record);
  }

  EXPECT_EQ(out_stream.str(),
            "{\"displayTimeUnit\":\"ns\",\"traceEvents\":["
            "],\"systemTraceEvents\":{\"type\":\"fuchsia\",\"events\":[]}}");
}

TEST(ChromiumExporterTest, UnknownLargeBlobAttachmentDropped) {
  static const char blob[] = "some test blob data";
  trace::Record record(trace::LargeRecordData{trace::LargeRecordData::BlobAttachment{
      "category",
      "no::UnknownName",
      blob,
      sizeof(blob),
  }});

  std::ostringstream out_stream;

  // Enclosing the exporter in its own scope ensures that its
  // cleanup routines are called by the destructor before the
  // output stream is read. This way, we can obtain the full
  // output rather than a truncated version.
  {
    tracing::ChromiumExporter exporter(out_stream);
    exporter.ExportRecord(record);
  }

  EXPECT_EQ(out_stream.str(),
            "{\"displayTimeUnit\":\"ns\",\"traceEvents\":["
            "],\"systemTraceEvents\":{\"type\":\"fuchsia\",\"events\":[]}}");
}

TEST(ChromiumExporterTest, FidlBlobExported) {
  static const char blob[] = "some test blob data";
  trace::Record record(trace::LargeRecordData{trace::LargeRecordData::BlobEvent{
      "fidl:blob",
      "BlobName",
      1000,
      trace::ProcessThread(45, 46),
      fbl::Vector<trace::Argument>(),
      blob,
      sizeof(blob),
  }});

  std::ostringstream out_stream;

  // Enclosing the exporter in its own scope ensures that its
  // cleanup routines are called by the destructor before the
  // output stream is read. This way, we can obtain the full
  // output rather than a truncated version.
  {
    tracing::ChromiumExporter exporter(out_stream);
    exporter.ExportRecord(record);
  }

  EXPECT_EQ(out_stream.str(),
            "{\"displayTimeUnit\":\"ns\",\"traceEvents\":[{\"ph\":\"O\",\"id\":\"\",\"cat\":\"fidl:"
            "blob\",\"name\":\"BlobName\",\"ts\":1.0,\"pid\":45,\"tid\":46,\"blob\":"
            "\"c29tZSB0ZXN0IGJsb2IgZGF0YQA=\"}],\"systemTraceEvents\":{\"type\":\"fuchsia\","
            "\"events\":[]}}");
}

TEST(ChromiumExporterTest, EmptyTrace) {
  std::ostringstream out_stream;

  // Enclosing the exporter in its own scope ensures that its
  // cleanup routines are called by the destructor before the
  // output stream is read. This way, we can obtain the full
  // output rather than a truncated version.
  { tracing::ChromiumExporter exporter(out_stream); }

  EXPECT_EQ(out_stream.str(),
            "{\"displayTimeUnit\":\"ns\",\"traceEvents\":["
            "],\"systemTraceEvents\":{\"type\":\"fuchsia\",\"events\":[]}}");
}

TEST(ChromiumExporterTest, LastBranchRecords) {
  const unsigned num_branches = 4;
  char blob[perfmon::LastBranchRecordBlobSize(num_branches)];
  auto lbr = reinterpret_cast<perfmon::LastBranchRecordBlob*>(blob);
  lbr->cpu = 1;
  lbr->num_branches = num_branches;
  lbr->reserved = 0;
  lbr->event_time = 1234;
  lbr->aspace = 4321;
  for (unsigned i = 0; i < num_branches; i++) {
    lbr->branches[i].from = 100 * i;
    lbr->branches[i].to = 100 * i + 50;
    lbr->branches[i].info = 69 * i;
  }
  trace::Record record(trace::Record::Blob{
      TRACE_BLOB_TYPE_LAST_BRANCH,
      fbl::String("cpu1"),
      blob,
      sizeof(blob),
  });

  std::ostringstream out_stream;

  // Enclosing the exporter in its own scope ensures that its
  // cleanup routines are called by the destructor before the
  // output stream is read. This way, we can obtain the full
  // output rather than a truncated version.
  {
    tracing::ChromiumExporter exporter(out_stream);
    exporter.ExportRecord(record);
  }

  EXPECT_EQ(
      out_stream.str(),
      "{\"displayTimeUnit\":\"ns\",\"traceEvents\":[],\"systemTraceEvents\":{\"type\":"
      "\"fuchsia\",\"events\":[]},\"lastBranch\":{\"records\":[{\"cpu\":1,\"aspace\":4321,"
      "\"event_time\":1234,\"branches\":[{\"from\":0,\"to\":50,\"info\":0},{\"from\":100,\"to\":"
      "150,\"info\":69},{\"from\":200,\"to\":250,\"info\":138},{\"from\":300,\"to\":350,\"info\":"
      "207}]}]}}");
}

}  // namespace
