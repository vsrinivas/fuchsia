// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "text_formatter.h"

#include <lib/fostr/hex_dump.h>

#include <sstream>

#include "lib/inspect/hierarchy.h"

namespace inspect {
namespace {
void Indent(std::ostream& ss, int indent) { ss << std::string(indent, ' '); }

constexpr size_t kMaxHexSize = 256;
std::string HexDump(const std::vector<uint8_t>& contents) {
  std::ostringstream out;
  if (contents.size() > kMaxHexSize) {
    out << "\nFirst " << kMaxHexSize << " bytes of " << contents.size() << ":";
  }
  out << fostr::HexDump(contents.data(), std::min(kMaxHexSize, contents.size()),
                        0x0);
  return out.str();
}

template <typename ArrayType>
void FormatArray(std::ostream& ss, const ArrayType& array) {
  auto buckets = array.GetBuckets();
  if (!buckets.empty()) {
    ss << "[";
    bool first = true;
    for (const auto& bucket : buckets) {
      if (!first) {
        ss << ", ";
      }
      first = false;

      // Prevent very small negative numbers from appearing in the underflow
      // bucket. Replace them with "<min>"
      if (bucket.floor != 0 &&
          bucket.floor == std::numeric_limits<decltype(bucket.floor)>::min()) {
        ss << "[<min>," << bucket.upper_limit << "]=";
      } else if (bucket.upper_limit != 0 &&
                 bucket.upper_limit ==
                     std::numeric_limits<decltype(bucket.upper_limit)>::max()) {
        ss << "[" << bucket.floor << ",<max>]=";
      }

      else {
        ss << "[" << bucket.floor << "," << bucket.upper_limit << "]=";
      }

      // Cast all counts to integers for display.
      if (!std::numeric_limits<decltype(bucket.count)>::is_integer) {
        ss << static_cast<uint64_t>(bucket.count);
      } else {
        ss << bucket.count;
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
      ss << val;
    }
    ss << "]";
  }
}

void FormatMetricValue(std::ostream& ss,
                       const inspect::hierarchy::Metric& metric) {
  switch (metric.format()) {
    case inspect::hierarchy::MetricFormat::INT_ARRAY:
      FormatArray(ss, metric.Get<inspect::hierarchy::IntArray>());
      break;
    case inspect::hierarchy::MetricFormat::UINT_ARRAY:
      FormatArray(ss, metric.Get<inspect::hierarchy::UIntArray>());
      break;
    case inspect::hierarchy::MetricFormat::DOUBLE_ARRAY:
      FormatArray(ss, metric.Get<inspect::hierarchy::DoubleArray>());
      break;
    case inspect::hierarchy::MetricFormat::INT:
      ss << metric.Get<inspect::hierarchy::IntMetric>().value();
      break;
    case inspect::hierarchy::MetricFormat::UINT:
      ss << metric.Get<inspect::hierarchy::UIntMetric>().value();
      break;
    case inspect::hierarchy::MetricFormat::DOUBLE:
      ss << metric.Get<inspect::hierarchy::DoubleMetric>().value();
      break;
    default:
      ss << "<unknown metric type>";
      break;
  }
}

}  // namespace

std::string TextFormatter::FormatSourcesRecursive(
    const std::vector<inspect::Source>& sources) const {
  std::ostringstream ss;
  ss.precision(6);
  ss << std::fixed;
  for (const auto& entry_point : sources) {
    entry_point.VisitObjectsInHierarchy([&](const Path path_to_node,
                                            const ObjectHierarchy& hierarchy) {
      const int name_indent = options_.indent * path_to_node.size();
      const int value_indent = name_indent + options_.indent;
      Indent(ss, name_indent);
      ss << FormatPathOrName(entry_point.GetLocation(), path_to_node,
                             hierarchy.node().name())
         << ":" << std::endl;

      for (const auto& property : hierarchy.node().properties()) {
        Indent(ss, value_indent);
        ss << property.name() << " = ";
        switch (property.format()) {
          case inspect::hierarchy::PropertyFormat::STRING:
            ss << property.Get<inspect::hierarchy::StringProperty>().value();
            break;
          case inspect::hierarchy::PropertyFormat::BYTES:
            ss << "Binary: "
               << HexDump(property.Get<inspect::hierarchy::ByteVectorProperty>()
                              .value());
            break;
          default:
            ss << "<unknown property format>";
            break;
        }
        ss << std::endl;
      }
      for (const auto& metric : hierarchy.node().metrics()) {
        Indent(ss, value_indent);
        ss << metric.name() << " = ";
        FormatMetricValue(ss, metric);
        ss << std::endl;
      }
    });
  }

  return ss.str();
}

std::string TextFormatter::FormatChildListing(
    const std::vector<inspect::Source>& sources) const {
  std::stringstream ss;
  for (const auto& source : sources) {
    const auto& hierarchy = source.GetHierarchy();
    for (const auto& child : hierarchy.children()) {
      ss << FormatPathOrName(source.GetLocation(), {child.node().name()},
                             child.node().name())
         << std::endl;
    }
  }
  return ss.str();
}

std::string TextFormatter::FormatSourceLocations(
    const std::vector<inspect::Source>& sources) const {
  std::stringstream ss;
  for (const auto& source : sources) {
    source.VisitObjectsInHierarchy(
        [&](const Path& path, const inspect::ObjectHierarchy& hierarchy) {
          ss << FormatPathOrName(source.GetLocation(), path,
                                 hierarchy.node().name())
             << std::endl;
        });
  }
  return ss.str();
}

}  // namespace inspect
