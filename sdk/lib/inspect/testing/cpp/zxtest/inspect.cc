// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/single_threaded_executor.h>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

namespace inspect {

void InspectTestHelper::ReadInspect(const zx::vmo& vmo) {
  hierarchy_ = inspect::ReadFromVmo(vmo);
  ASSERT_TRUE(hierarchy_.is_ok());
}

void InspectTestHelper::ReadInspect(const inspect::Inspector& inspector) {
  fpromise::single_threaded_executor executor;
  // Reset hierarchy_.
  hierarchy_ = fpromise::result<inspect::Hierarchy>();
  executor.schedule_task(inspect::ReadFromInspector(inspector).then(
      [&](fpromise::result<inspect::Hierarchy>& res) { hierarchy_ = std::move(res); }));
  executor.run();
  ASSERT_TRUE(hierarchy_.is_ok());
}

void InspectTestHelper::PrintAllProperties(const inspect::NodeValue& node, unsigned indent) {
  const auto& props = node.properties();
  auto* log_sink = zxtest::Runner::GetInstance()->mutable_reporter()->mutable_log_sink();
  for (const auto& p : props) {
    log_sink->Write("%*c\b", indent, ' ');
    log_sink->Write("%s : ", p.name().c_str());
    switch (p.format()) {
      case inspect::PropertyFormat::kInt:
        log_sink->Write("%ld\n", p.Get<inspect::IntPropertyValue>().value());
        break;
      case inspect::PropertyFormat::kUint:
        log_sink->Write("%lu\n", p.Get<inspect::UintPropertyValue>().value());
        break;
      case inspect::PropertyFormat::kDouble:
        log_sink->Write("%lf\n", p.Get<inspect::DoublePropertyValue>().value());
        break;
      case inspect::PropertyFormat::kBool:
        log_sink->Write("%d\n", p.Get<inspect::BoolPropertyValue>().value());
        break;
      case inspect::PropertyFormat::kIntArray:
        log_sink->Write("{ ");
        for (auto i : p.Get<inspect::IntArrayValue>().value()) {
          log_sink->Write("%ld ", i);
        }
        log_sink->Write("}\n");
        break;
      case inspect::PropertyFormat::kUintArray:
        log_sink->Write("{ ");
        for (auto i : p.Get<inspect::UintArrayValue>().value()) {
          log_sink->Write("%lu ", i);
        }
        log_sink->Write("}\n");
        break;
      case inspect::PropertyFormat::kDoubleArray:
        log_sink->Write("{ ");
        for (auto i : p.Get<inspect::DoubleArrayValue>().value()) {
          log_sink->Write("%lf ", i);
        }
        log_sink->Write("}\n");
        break;
      case inspect::PropertyFormat::kString:
        log_sink->Write("%s\n", p.Get<inspect::StringPropertyValue>().value().c_str());
        break;
      case inspect::PropertyFormat::kBytes:
        log_sink->Write("{ ");
        for (auto i : p.Get<inspect::ByteVectorPropertyValue>().value()) {
          log_sink->Write("%02x ", i);
        }
        log_sink->Write("}\n");
        break;
      default:
        log_sink->Write("format not supported\n");
        break;
    }
  }
}

void InspectTestHelper::PrintAllProperties(const inspect::Hierarchy& hierarchy) {
  auto* log_sink = zxtest::Runner::GetInstance()->mutable_reporter()->mutable_log_sink();
  std::deque<std::pair<const Hierarchy*, int>> to_visit{std::make_pair(&hierarchy, 1)};
  while (!to_visit.empty()) {
    const auto [next_hierarchy, indent] = to_visit.front();
    log_sink->Write("%*c\b%s: \n", indent, ' ', next_hierarchy->name().c_str());
    PrintAllProperties(next_hierarchy->node(), indent + 2);
    // Add the children of our level to the front of the list in proper order
    // before advancing to the next hierarchy.
    to_visit.pop_front();
    for (size_t child = next_hierarchy->children().size(); child > 0; child--) {
      to_visit.emplace_front(&next_hierarchy->children()[child - 1], indent + 2);
    }
  }
}

}  // namespace inspect
