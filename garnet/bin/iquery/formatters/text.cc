// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/iquery/formatters/text.h"

#include <src/lib/fxl/strings/string_printf.h>

#include <cstdio>
#include <sstream>

#include "garnet/bin/iquery/options.h"
#include "lib/inspect/hierarchy.h"

namespace iquery {

namespace {

#define INDENT_SIZE 2

inline std::string Indent(int indent) {
  return std::string(indent * INDENT_SIZE, ' ');
}

template <typename ArrayType>
std::string FormatArray(const ArrayType& array) {
  std::ostringstream ss;
  auto buckets = array.GetBuckets();
  if (!buckets.empty()) {
    ss << "[";
    bool first = true;
    for (const auto& bucket : buckets) {
      if (!first) {
        ss << ", ";
      }
      first = false;
      if (bucket.floor != 0 &&
          bucket.floor == std::numeric_limits<decltype(bucket.floor)>::min()) {
        ss << "<min>=" << bucket.count;
      } else {
        ss << FormatNumericValue(bucket.floor) << "=" << bucket.count;
      }
    }
    ss << "]";
  } else {
    ss << "[";
    bool first = true;
    for (const auto& val : array.value()) {
      if (!first) {
        ss << ", ";
      }
      first = false;
      ss << FormatNumericValue(val);
    }
    ss << "]";
  }
  return ss.str();
}

std::string FormatMetricValue(const inspect::hierarchy::Metric& metric) {
  switch (metric.format()) {
    case inspect::hierarchy::MetricFormat::INT_ARRAY:
      return FormatArray(metric.Get<inspect::hierarchy::IntArray>());
    case inspect::hierarchy::MetricFormat::UINT_ARRAY:
      return FormatArray(metric.Get<inspect::hierarchy::UIntArray>());
    case inspect::hierarchy::MetricFormat::DOUBLE_ARRAY:
      return FormatArray(metric.Get<inspect::hierarchy::DoubleArray>());
    default:
      return FormatNumericMetricValue(metric);
  }
}

// This version exists so we can pass in the indentation and path from the entry
// point.
std::string RecursiveFormatCat(const Options& options,
                               const inspect::Source& entry_point,
                               const inspect::ObjectHierarchy& root,
                               std::vector<std::string>* path) {
  // In each step of the indentation we output the path formatting. This is not
  // equivalent to what formatters as JSON do, which will introduce a path
  // entry under each object.
  // This is mainly because this formatter is intended for human examination and
  // it's not intended for easier parsing.
  std::ostringstream ss;
  size_t indent = path->size() + 1;
  for (const auto& property : root.node().properties()) {
    ss << Indent(indent) << FormatStringHexFallback(property.name()) << " = ";

    if (property.Contains<inspect::hierarchy::StringProperty>()) {
      ss << FormatStringHexFallback(
                property.Get<inspect::hierarchy::StringProperty>().value())
         << std::endl;
    } else if (property.Contains<inspect::hierarchy::ByteVectorProperty>()) {
      auto& val =
          property.Get<inspect::hierarchy::ByteVectorProperty>().value();
      ss << FormatStringHexFallback(
                {reinterpret_cast<const char*>(val.data()), val.size()})
         << std::endl;
    } else {
      ss << "<Unknown property format>" << std::endl;
    }
  }

  for (const auto& metric : root.node().metrics()) {
    ss << Indent(indent) << FormatStringHexFallback(metric.name()) << " = "
       << FormatMetricValue(metric) << std::endl;
  }

  // We print recursively. The recursive nature of the cat called is already
  // taken care of by now.
  for (const auto& child : root.children()) {
    path->push_back(child.node().name());
    ss << Indent(indent)
       << FormatPath(options.path_format,
                     entry_point.GetLocation().NodePath(*path),
                     child.node().name())
       << ":" << std::endl;
    ss << RecursiveFormatCat(options, entry_point, child, path);
    path->pop_back();
  }

  return ss.str();
}

std::string FormatFind(const Options& options,
                       const std::vector<inspect::Source>& results) {
  std::stringstream ss;
  for (const auto& entry_point : results) {
    entry_point.VisitObjectsInHierarchy(
        [&](const std::vector<std::string>& path,
            const inspect::ObjectHierarchy& hierarchy) {
          ss << FormatPath(options.path_format,
                           entry_point.GetLocation().NodePath(path),
                           hierarchy.node().name())
             << std::endl;
        });
  }
  return ss.str();
}

std::string FormatLs(const Options& options,
                     const std::vector<inspect::Source>& results) {
  std::stringstream ss;
  for (const auto& entry_point : results) {
    const auto& hierarchy = entry_point.GetHierarchy();
    for (const auto& child : hierarchy.children()) {
      ss << FormatPath(
                options.path_format,
                entry_point.GetLocation().NodePath({child.node().name()}),
                child.node().name())
         << std::endl;
    }
  }
  return ss.str();
}

std::string FormatCat(const Options& options,
                      const std::vector<inspect::Source>& results) {
  std::ostringstream ss;
  for (const auto& entry_point : results) {
    const auto& hierarchy = entry_point.GetHierarchy();
    ss << FormatPath(options.path_format, entry_point.GetLocation().NodePath(),
                     hierarchy.node().name())
       << ":" << std::endl;
    std::vector<std::string> path_holder;
    ss << RecursiveFormatCat(options, entry_point, hierarchy, &path_holder);
  }

  return ss.str();
}

}  // namespace

std::string TextFormatter::Format(const Options& options,
                                  const std::vector<inspect::Source>& results) {
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
