// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "json_formatter.h"

#include <locale>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>

#include "rapidjson/stringbuffer.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/inspect_deprecated/health/health.h"
#include "src/lib/inspect_deprecated/hierarchy.h"
#include "third_party/modp_b64/modp_b64.h"

namespace inspect_deprecated {

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
void FormatMetricValue(WriterType& writer, const inspect_deprecated::hierarchy::Metric& metric) {
  switch (metric.format()) {
    case inspect_deprecated::hierarchy::MetricFormat::INT_ARRAY:
      FormatArray(writer, metric.Get<inspect_deprecated::hierarchy::IntArray>());
      break;
    case inspect_deprecated::hierarchy::MetricFormat::UINT_ARRAY:
      FormatArray(writer, metric.Get<inspect_deprecated::hierarchy::UIntArray>());
      break;
    case inspect_deprecated::hierarchy::MetricFormat::DOUBLE_ARRAY:
      FormatArray(writer, metric.Get<inspect_deprecated::hierarchy::DoubleArray>());
      break;
    case inspect_deprecated::hierarchy::MetricFormat::INT:
      FormatNumericValue(writer, metric.Get<inspect_deprecated::hierarchy::IntMetric>().value());
      break;
    case inspect_deprecated::hierarchy::MetricFormat::UINT:
      FormatNumericValue(writer, metric.Get<inspect_deprecated::hierarchy::UIntMetric>().value());
      break;
    case inspect_deprecated::hierarchy::MetricFormat::DOUBLE:
      FormatNumericValue(writer, metric.Get<inspect_deprecated::hierarchy::DoubleMetric>().value());
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
void InternalFormatSource(WriterType& writer, const inspect_deprecated::Source& source,
                          const inspect_deprecated::ObjectHierarchy& root) {
  writer->StartObject();

  // Properties.
  for (const auto& property : root.node().properties()) {
    writer->String(property.name());
    switch (property.format()) {
      case inspect_deprecated::hierarchy::PropertyFormat::STRING:
        writer->String(property.Get<inspect_deprecated::hierarchy::StringProperty>().value());
        break;
      case inspect_deprecated::hierarchy::PropertyFormat::BYTES: {
        auto& val = property.Get<inspect_deprecated::hierarchy::ByteVectorProperty>().value();
        std::string content(modp_b64_encode_len(val.size()), '\0');
        modp_b64_encode(const_cast<char*>(content.data()),
                        reinterpret_cast<const char*>(val.data()), val.size());
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

template <typename WriterType>
void WriteJsonForHealthNode(const std::string& node_name,
                            const inspect_deprecated::ObjectHierarchy& node, WriterType& writer) {
  std::string status;
  std::string message;
  for (auto& property : node.node().properties()) {
    if (property.name() == "status")
      status = property.Get<inspect_deprecated::hierarchy::StringProperty>().value();
    if (property.name() == "message")
      message = property.Get<inspect_deprecated::hierarchy::StringProperty>().value();
  }

  FXL_DCHECK(!status.empty());

  writer.String(node_name);
  writer.StartObject();
  writer.String("status");
  writer.String(status);
  if (!message.empty()) {
    writer.String("message");
    writer.String(message);
  }
  writer.EndObject();
}

}  // namespace

template <typename WriterType>
void JsonFormatter::InternalFormatSourceLocations(
    WriterType& writer, const std::vector<inspect_deprecated::Source>& sources) const {
  writer->StartArray();
  for (const auto& source : sources) {
    source.VisitObjectsInHierarchy(
        [&](const Path& path, const inspect_deprecated::ObjectHierarchy& hierarchy) {
          writer->String(FormatPathOrName(source.GetLocation(), path, hierarchy.node().name()));
        });
  }
  writer->EndArray();
}

std::string JsonFormatter::FormatSourceLocations(
    const std::vector<inspect_deprecated::Source>& sources) const {
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
    WriterType& writer, const std::vector<inspect_deprecated::Source>& sources) const {
  writer->StartArray();
  for (const auto& source : sources) {
    const auto& hierarchy = source.GetHierarchy();
    for (const auto& child : hierarchy.children()) {
      writer->String(
          FormatPathOrName(source.GetLocation(), {child.node().name()}, child.node().name()));
    }
  }
  writer->EndArray();
}

std::string JsonFormatter::FormatChildListing(
    const std::vector<inspect_deprecated::Source>& sources) const {
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
    WriterType& writer, const std::vector<inspect_deprecated::Source>& sources) const {
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
    const std::vector<inspect_deprecated::Source>& sources) const {
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

std::string JsonFormatter::FormatHealth(
    const std::vector<inspect_deprecated::Source>& sources) const {
  // Write to a pretty string.
  rapidjson::StringBuffer buffer;
  if (options_.indent == 0) {
    auto writer = GetJsonWriter(buffer, options_);
    InternalFormatHealth(*writer, sources);
  } else {
    auto writer = GetPrettyJsonWriter(buffer, options_);
    InternalFormatHealth(*writer, sources);
  }

  auto output = buffer.GetString();
  return output;
}

template <typename WriterType>
void JsonFormatter::InternalFormatHealth(
    WriterType& writer, const std::vector<inspect_deprecated::Source>& sources) const {
  writer.StartObject();
  for (const auto& entry_point : sources) {
    entry_point.VisitObjectsInHierarchy(
        [&](const auto& path_to_node, const ObjectHierarchy& hierarchy) {
          // GetByPath returns nullptr if not found.
          const ObjectHierarchy* health_node =
              hierarchy.GetByPath({inspect_deprecated::kHealthNodeName});
          if (!health_node)
            return;

          std::string node_name =
              FormatPathOrName(entry_point.GetLocation(), path_to_node, hierarchy.node().name());

          WriteJsonForHealthNode(node_name, *health_node, writer);
        });
  }
  writer.EndObject();
}

}  // namespace inspect_deprecated
