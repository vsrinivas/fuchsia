// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "json_formatter.h"

#include <rapidjson/prettywriter.h>
#include <third_party/cobalt/util/crypto_util/base64.h>

#include <locale>

#include "lib/inspect/hierarchy.h"
#include "rapidjson/stringbuffer.h"

using cobalt::crypto::Base64Encode;

namespace inspect {

namespace {

constexpr size_t kMaxDecimalPlaces = 6;

template <typename WriterType>
void FormatNumericValue(WriterType& writer, int64_t value) {
  writer->Int64(value);
}

template <typename WriterType>
void FormatNumericValue(WriterType& writer, uint64_t value) {
  writer->Uint64(value);
}

template <typename WriterType>
void FormatNumericValue(WriterType& writer, double value) {
  if (value == std::numeric_limits<double>::infinity()) {
    writer->String("Infinity");
  } else if (value == -std::numeric_limits<double>::infinity()) {
    writer->String("-Infinity");
  } else if (value == std::numeric_limits<double>::quiet_NaN()) {
    writer->String("NaN");
  } else {
    writer->Double(value);
  }
}

// Properly formats an array metric based on its display flags.
template <typename WriterType, typename ArrayType>
void FormatArray(WriterType& writer, const ArrayType& array) {
  auto buckets = array.GetBuckets();
  if (buckets.size() != 0) {
    writer->StartObject();
    writer->String("buckets");
    writer->StartArray();
    for (const auto& bucket : buckets) {
      writer->StartObject();
      writer->String("floor");
      FormatNumericValue(writer, bucket.floor);
      writer->String("upper_bound");
      FormatNumericValue(writer, bucket.upper_limit);
      writer->String("count");
      FormatNumericValue(writer, bucket.count);
      writer->EndObject();
    }
    writer->EndArray();
    writer->EndObject();
  } else {
    writer->StartArray();
    for (const auto& val : array.value()) {
      FormatNumericValue(writer, val);
    }
    writer->EndArray();
  }
}

// Properly formats a metric based on its type.
template <typename WriterType>
void FormatMetricValue(WriterType& writer,
                       const inspect::hierarchy::Metric& metric) {
  switch (metric.format()) {
    case inspect::hierarchy::MetricFormat::INT_ARRAY:
      FormatArray(writer, metric.Get<inspect::hierarchy::IntArray>());
      break;
    case inspect::hierarchy::MetricFormat::UINT_ARRAY:
      FormatArray(writer, metric.Get<inspect::hierarchy::UIntArray>());
      break;
    case inspect::hierarchy::MetricFormat::DOUBLE_ARRAY:
      FormatArray(writer, metric.Get<inspect::hierarchy::DoubleArray>());
      break;
    case inspect::hierarchy::MetricFormat::INT:
      FormatNumericValue(writer,
                         metric.Get<inspect::hierarchy::IntMetric>().value());
      break;
    case inspect::hierarchy::MetricFormat::UINT:
      FormatNumericValue(writer,
                         metric.Get<inspect::hierarchy::UIntMetric>().value());
      break;
    case inspect::hierarchy::MetricFormat::DOUBLE:
      FormatNumericValue(
          writer, metric.Get<inspect::hierarchy::DoubleMetric>().value());
      break;
    default:
      writer->String("<unknown metric format>");
  }
}

// Create a pretty format JSON writer that indents its output.
template <typename OutputStream>
std::unique_ptr<rapidjson::PrettyWriter<OutputStream>> GetPrettyJsonWriter(
    OutputStream& os, const JsonFormatter::Options& options) {
  auto ret = std::make_unique<rapidjson::PrettyWriter<OutputStream>>(os);
  ret->SetMaxDecimalPlaces(kMaxDecimalPlaces);
  ret->SetIndent(' ', options.indent);
  return ret;
}

// Create a plain JSON writer that doesn't pretty format.
template <typename OutputStream>
std::unique_ptr<rapidjson::Writer<OutputStream>> GetJsonWriter(
    OutputStream& os, const JsonFormatter::Options& options) {
  auto ret = std::make_unique<rapidjson::Writer<OutputStream>>(os);
  ret->SetMaxDecimalPlaces(kMaxDecimalPlaces);
  return ret;
}

// Internal function to recursively format a hierarchy rooted at a given source.
template <typename WriterType>
void InternalFormatSource(WriterType& writer, const inspect::Source& source,
                          const inspect::ObjectHierarchy& root) {
  writer->StartObject();

  // Properties.
  for (const auto& property : root.node().properties()) {
    writer->String(property.name());
    switch (property.format()) {
      case inspect::hierarchy::PropertyFormat::STRING:
        writer->String(
            property.Get<inspect::hierarchy::StringProperty>().value());
        break;
      case inspect::hierarchy::PropertyFormat::BYTES: {
        auto& val =
            property.Get<inspect::hierarchy::ByteVectorProperty>().value();
        std::string content;
        Base64Encode((uint8_t*)val.data(), val.size(), &content);
        writer->String("b64:" + content);
        break;
      }
      default:
        writer->String("<Unknown type, format failed>");
        break;
    }
  }

  // Metrics.
  for (const auto& metric : root.node().metrics()) {
    writer->String(metric.name());
    FormatMetricValue(writer, metric);
  }

  for (const auto& child : root.children()) {
    writer->String(child.node().name());
    InternalFormatSource(writer, source, child);
  }

  writer->EndObject();
}

}  // namespace

template <typename WriterType>
void JsonFormatter::InternalFormatSourceLocations(
    WriterType& writer, const std::vector<inspect::Source>& sources) const {
  writer->StartArray();
  for (const auto& source : sources) {
    source.VisitObjectsInHierarchy(
        [&](const Path& path, const inspect::ObjectHierarchy& hierarchy) {
          writer->String(FormatPathOrName(source.GetLocation(), path,
                                          hierarchy.node().name()));
        });
  }
  writer->EndArray();
}

std::string JsonFormatter::FormatSourceLocations(
    const std::vector<inspect::Source>& sources) const {
  rapidjson::StringBuffer buffer;
  if (options_.indent == 0) {
    auto writer = GetJsonWriter(buffer, options_);
    InternalFormatSourceLocations(writer, sources);
  } else {
    auto writer = GetPrettyJsonWriter(buffer, options_);
    InternalFormatSourceLocations(writer, sources);
  }
  return buffer.GetString();
}

template <typename WriterType>
void JsonFormatter::InternalFormatChildListing(
    WriterType& writer, const std::vector<inspect::Source>& sources) const {
  writer->StartArray();
  for (const auto& source : sources) {
    const auto& hierarchy = source.GetHierarchy();
    for (const auto& child : hierarchy.children()) {
      writer->String(FormatPathOrName(
          source.GetLocation(), {child.node().name()}, child.node().name()));
    }
  }
  writer->EndArray();
}

std::string JsonFormatter::FormatChildListing(
    const std::vector<inspect::Source>& sources) const {
  rapidjson::StringBuffer buffer;
  if (options_.indent == 0) {
    auto writer = GetJsonWriter(buffer, options_);
    InternalFormatChildListing(writer, sources);
  } else {
    auto writer = GetPrettyJsonWriter(buffer, options_);
    InternalFormatChildListing(writer, sources);
  }
  return buffer.GetString();
}

template <typename WriterType>
void JsonFormatter::InternalFormatSourcesRecursive(
    WriterType& writer, const std::vector<inspect::Source>& sources) const {
  writer->StartArray();
  for (const auto& source : sources) {
    writer->StartObject();
    writer->String("path");
    writer->String(FormatPath(source.GetLocation(), {}));

    writer->String("contents");
    writer->StartObject();
    writer->String(source.GetHierarchy().node().name());
    InternalFormatSource(writer, source, source.GetHierarchy());
    writer->EndObject();  // contents
    writer->EndObject();  // source
  }
  writer->EndArray();
}

std::string JsonFormatter::FormatSourcesRecursive(
    const std::vector<inspect::Source>& sources) const {
  rapidjson::StringBuffer buffer;
  if (options_.indent == 0) {
    auto writer = GetJsonWriter(buffer, options_);
    InternalFormatSourcesRecursive(writer, sources);
  } else {
    auto writer = GetPrettyJsonWriter(buffer, options_);
    InternalFormatSourcesRecursive(writer, sources);
  }
  return buffer.GetString();
}

}  // namespace inspect
