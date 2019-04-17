// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/iquery/formatters/json.h"

#include <rapidjson/prettywriter.h>

#include "garnet/bin/iquery/utils.h"
#include "lib/inspect/hierarchy.h"
#include "third_party/cobalt/util/crypto_util/base64.h"

namespace iquery {

namespace {

using cobalt::crypto::Base64Encode;

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
      writer->String(FormatNumericValue(bucket.floor));
      writer->String("upper_bound");
      writer->String(FormatNumericValue(bucket.upper_limit));
      writer->String("count");
      writer->String(FormatNumericValue(bucket.count));
      writer->EndObject();
    }
    writer->EndArray();
    writer->EndObject();
  } else {
    writer->StartArray();
    for (const auto& val : array.value()) {
      writer->String(FormatNumericValue(val));
    }
    writer->EndArray();
  }
}

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
    default:
      writer->String(FormatNumericMetricValue(metric));
  }
}

// Create the appropriate Json exporter according to the given options.
// NOTE(donoso): For some reason, rapidjson decided that while Writer is a class
//               PrettyWriter inherits from, it is *not* a virtual interface.
//               This means I cannot pass a Writer pointer around and get each
//               writer to do it's thing, which is what I expected.
//               Eventually this class will have to become a dispatcher that
//               passes the writer to a templatized version of FormatCat, Ls
//               and Find.
template <typename OutputStream>
std::unique_ptr<rapidjson::PrettyWriter<OutputStream>> GetJsonWriter(
    OutputStream& os, const Options&) {
  // NOTE(donosoc): When json formatter options are given, create the
  //                appropriate json writer and configure it here.
  return std::make_unique<rapidjson::PrettyWriter<OutputStream>>(os);
}

// FormatFind ------------------------------------------------------------------

std::string FormatFind(const Options& options,
                       const std::vector<inspect::ObjectSource>& results) {
  rapidjson::StringBuffer sb;
  auto writer = GetJsonWriter(sb, options);

  writer->StartArray();
  for (const auto& entry_point : results) {
    entry_point.VisitObjectsInHierarchy(
        [&](const std::vector<std::string>& path,
            const inspect::ObjectHierarchy& hierarchy) {
          writer->String(FormatPath(options.path_format,
                                    entry_point.FormatRelativePath(path),
                                    hierarchy.node().name()));
        });
  }
  writer->EndArray();

  return sb.GetString();
}

// FormatLs --------------------------------------------------------------------

std::string FormatLs(const Options& options,
                     const std::vector<inspect::ObjectSource>& results) {
  rapidjson::StringBuffer sb;
  auto writer = GetJsonWriter(sb, options);

  writer->StartArray();
  for (const auto& entry_point : results) {
    const auto& hierarchy = entry_point.GetRootHierarchy();
    for (const auto& child : hierarchy.children()) {
      writer->String(
          FormatPath(options.path_format,
                     entry_point.FormatRelativePath({child.node().name()}),
                     child.node().name()));
    }
  }
  writer->EndArray();

  return sb.GetString();
}

// FormatCat -------------------------------------------------------------------

template <typename OutputStream>
void RecursiveFormatCat(rapidjson::PrettyWriter<OutputStream>* writer,
                        const Options& options,
                        const inspect::ObjectSource& entry_point,
                        const inspect::ObjectHierarchy& root) {
  writer->StartObject();

  // Properties.
  for (const auto& property : root.node().properties()) {
    writer->String(FormatStringBase64Fallback(property.name()));
    switch (property.format()) {
      case inspect::hierarchy::PropertyFormat::STRING:
        writer->String(FormatStringBase64Fallback(
            property.Get<inspect::hierarchy::StringProperty>().value()));
        break;
      case inspect::hierarchy::PropertyFormat::BYTES: {
        auto& val =
            property.Get<inspect::hierarchy::ByteVectorProperty>().value();
        writer->String(FormatStringBase64Fallback(
            {reinterpret_cast<const char*>(val.data()), val.size()}));
        break;
      }
      default:
        FXL_LOG(WARNING) << "Failed to format unknown type for "
                         << property.name();
        writer->String("<Unknown type, format failed>");
        break;
    }
  }

  // Metrics.
  for (const auto& metric : root.node().metrics()) {
    writer->String(FormatStringBase64Fallback(metric.name()));
    FormatMetricValue(writer, metric);
  }

  for (const auto& child : root.children()) {
    writer->String(child.node().name());
    RecursiveFormatCat(writer, options, entry_point, child);
  }

  writer->EndObject();
}

std::string FormatCat(const Options& options,
                      const std::vector<inspect::ObjectSource>& results) {
  rapidjson::StringBuffer sb;
  auto writer = GetJsonWriter(sb, options);

  writer->StartArray();
  for (const auto& entry_point : results) {
    writer->StartObject();
    writer->String("path");
    // The "path" field always ignored the object's name in JSON output.
    writer->String(FormatPath(options.path_format,
                              entry_point.FormatRelativePath(),
                              entry_point.FormatRelativePath()));
    writer->String("contents");
    writer->StartObject();
    writer->String(entry_point.GetRootHierarchy().node().name());
    RecursiveFormatCat(writer.get(), options, entry_point,
                       entry_point.GetRootHierarchy());
    writer->EndObject();
    writer->EndObject();
  }
  writer->EndArray();

  return sb.GetString();
}

}  // namespace

std::string JsonFormatter::Format(
    const Options& options, const std::vector<inspect::ObjectSource>& results) {
  switch (options.mode) {
    case Options::Mode::CAT:
      return FormatCat(options, results);
    case Options::Mode::FIND:
      return FormatFind(options, results);
    case Options::Mode::LS:
      return FormatLs(options, results);
    case Options::Mode::UNSET: {
      FXL_LOG(ERROR) << "Unset Mode";
      return "";
    }
  }
}

}  // namespace iquery
