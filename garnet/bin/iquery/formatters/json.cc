// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rapidjson/prettywriter.h>

#include "garnet/bin/iquery/formatters/json.h"
#include "garnet/bin/iquery/utils.h"

#include "third_party/cobalt/util/crypto_util/base64.h"

namespace iquery {

namespace {

using cobalt::crypto::Base64Encode;

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
                                    hierarchy.object().name));
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
                     entry_point.FormatRelativePath({child.object().name}),
                     child.object().name));
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
  for (const auto& property : *root.object().properties) {
    writer->String(FormatStringBase64Fallback(property.key));
    if (property.value.is_str()) {
      writer->String(FormatStringBase64Fallback(property.value.str()));
    } else {
      auto& val = property.value.bytes();
      writer->String(
          FormatStringBase64Fallback({(char*)val.data(), val.size()}));
    }
  }

  // Metrics.
  for (const auto& metric : *root.object().metrics) {
    writer->String(FormatStringBase64Fallback(metric.key));
    writer->String(FormatMetricValue(metric));
  }

  for (const auto& child : root.children()) {
    writer->String(child.object().name);
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
    writer->String(entry_point.GetRootHierarchy().object().name);
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
