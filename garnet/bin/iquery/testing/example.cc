// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/inspect/component.h>
#include <lib/sys/cpp/component_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/strings/string_printf.h>

#include <variant>
#include <vector>

#include "lib/inspect/inspect.h"

size_t current_suffix = 0;
void ResetUniqueNames() { current_suffix = 0; }
std::string UniqueName(const char* name) {
  return fxl::StringPrintf("%s:0x%lx", name, current_suffix++);
}

// A single cell in the table.
// Cells expose a string property, an int metric, and a double metric.
class Cell {
 public:
  Cell(const std::string& name, int64_t value, double double_value,
       inspect::Object obj)
      : object_(std::move(obj)) {
    name_ = object_.CreateStringProperty("name", name);
    value_ = object_.CreateIntMetric("value", value);
    double_value_ = object_.CreateDoubleMetric("double_value", double_value);
  }

 private:
  inspect::Object object_;
  inspect::StringProperty name_;
  inspect::IntMetric value_;
  inspect::DoubleMetric double_value_;
};

// A row in the table, contains cells.
class Row {
 public:
  explicit Row(inspect::Object obj) : object_(std::move(obj)) {}

  Row(Row&&) = default;
  Row& operator=(Row&&) = default;

  Cell* AddCell(const std::string& name, int64_t value, double double_value) {
    // Construct a new cell in-place, and expose it as a child of this object in
    // the Inspect output.
    cells_.push_back(Cell(name, value, double_value,
                          object_.CreateChild(UniqueName("cell"))));
    return &(*cells_.rbegin());
  }

 private:
  inspect::Object object_;
  std::vector<Cell> cells_;
};

// A table, contains rows.
class Table {
 public:
  Table(int row_count, int col_count, inspect::Object obj)
      : object_(std::move(obj)) {
    object_name_ = object_.CreateStringProperty("object_name", "Example Table");
    binary_data_ = object_.CreateByteVectorProperty(
        "binary_data", std::vector<uint8_t>({0x20, 0x0, 0x11, 0x12, 0x5}));
    binary_key_ = object_.CreateStringProperty(
        std::string("\x05\x01\x03", 3),
        "The key of this value is a binary value.");
    binary_key_and_data_ = object_.CreateByteVectorProperty(
        std::string("\x05\x01\x02", 3), std::vector<uint8_t>({0x1, 0x2}));

    // Add default rows and columns to the table.
    const int total = row_count * col_count;
    int idx = 0;
    for (int i = 0; i < row_count; ++i) {
      Row* row = AddRow();
      for (int j = 0; j < col_count; ++j) {
        // name is "(row,col)", value is i*j, and double_value is percentage
        // done.
        row->AddCell(fxl::StringPrintf("(%d,%d)", i, j), i * j,
                     100.0 * (++idx) / total);
      }
    }
  }
  Row* AddRow() {
    // Construct a new row in-place, and expose it as a child of this object in
    // the Inspect output.
    rows_.push_back(Row(object_.CreateChild(UniqueName("row"))));
    return &(*rows_.rbegin());
  }

 private:
  inspect::Object object_;
  inspect::StringProperty object_name_;
  inspect::ByteVectorProperty binary_data_;
  inspect::StringProperty binary_key_;
  inspect::ByteVectorProperty binary_key_and_data_;
  std::vector<Row> rows_;
};

template <typename HistogramType, typename NumericType>
HistogramType PopulatedHistogram(HistogramType histogram, NumericType floor,
                                 NumericType step, size_t count) {
  for (size_t i = 0; i < count; i++) {
    histogram.Insert(floor);
    floor += step;
  }
  return histogram;
}

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  // Construct a demo table with the rows and columns given on the command line.
  auto rows = command_line.GetOptionValueWithDefault("rows", "");
  auto columns = command_line.GetOptionValueWithDefault("columns", "");
  if (rows.empty() || columns.empty() || atoi(rows.c_str()) == 0 ||
      atoi(columns.c_str()) == 0) {
    fprintf(stderr, R"text(Usage: %s --rows=N --columns=M
  Example component to showcase Inspect API objects, including an NxM
  nested table.
)text",
            argv[0]);
    return 1;
  }

  // Exposing objects requires a loop and the startup context.
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto component_context = sys::ComponentContext::Create();

  // Legacy work required to expose an object tree over FIDL.
  auto root = component::ObjectDir::Make("root");
  fidl::BindingSet<fuchsia::inspect::Inspect> inspect_bindings_;
  component_context->outgoing()->GetOrCreateDirectory("objects")->AddEntry(
      fuchsia::inspect::Inspect::Name_,
      std::make_unique<vfs::Service>(
          inspect_bindings_.GetHandler(root.object().get())));
  auto root_object_fidl = inspect::Object(root);

  auto inspector =
      inspect::ComponentInspector::Initialize(component_context.get());
  auto& root_object_vmo = inspector->root_tree()->GetRoot();

  // Storage for the two different table implementations, for ensuring they are
  // the same.
  std::vector<Table> tables;

  const int row_count = atoi(rows.c_str());
  const int col_count = atoi(columns.c_str());

  std::vector<
      std::variant<inspect::IntArray, inspect::UIntArray, inspect::DoubleArray>>
      arrays;
  std::vector<std::variant<inspect::LinearIntHistogramMetric,
                           inspect::LinearUIntHistogramMetric,
                           inspect::LinearDoubleHistogramMetric,
                           inspect::ExponentialIntHistogramMetric,
                           inspect::ExponentialUIntHistogramMetric,
                           inspect::ExponentialDoubleHistogramMetric>>
      histograms;

  for (auto* root :
       std::vector<inspect::Object*>({&root_object_fidl, &root_object_vmo})) {
    ResetUniqueNames();
    tables.emplace_back(row_count, col_count,
                        root->CreateChild(UniqueName("table")));

    {
      auto array = root->CreateIntArray(UniqueName("array"), 3);
      array.Set(0, 1);
      array.Add(1, 10);
      array.Subtract(2, 3);
      arrays.emplace_back(std::move(array));
    }

    {
      auto array = root->CreateUIntArray(UniqueName("array"), 3);
      array.Set(0, 1);
      array.Add(1, 10);
      array.Set(2, 3);
      array.Subtract(2, 1);
      arrays.emplace_back(std::move(array));
    }

    {
      auto array = root->CreateDoubleArray(UniqueName("array"), 3);
      array.Set(0, 0.25);
      array.Add(1, 1.25);
      array.Subtract(2, 0.75);
      arrays.emplace_back(std::move(array));
    }

    histograms.emplace_back(
        PopulatedHistogram(root->CreateLinearIntHistogramMetric(
                               UniqueName("histogram"), -10, 5, 3),
                           -20, 1, 40));
    histograms.emplace_back(PopulatedHistogram(
        root->CreateLinearUIntHistogramMetric(UniqueName("histogram"), 5, 5, 3),
        0, 1, 40));
    histograms.emplace_back(
        PopulatedHistogram(root->CreateLinearDoubleHistogramMetric(
                               UniqueName("histogram"), 0, .5, 3),
                           -1.0, .1, 40.0));
    histograms.emplace_back(
        PopulatedHistogram(root->CreateExponentialIntHistogramMetric(
                               UniqueName("histogram"), -10, 5, 2, 3),
                           -20, 1, 40));
    histograms.emplace_back(
        PopulatedHistogram(root->CreateExponentialUIntHistogramMetric(
                               UniqueName("histogram"), 1, 1, 2, 3),
                           0, 1, 40));
    histograms.emplace_back(
        PopulatedHistogram(root->CreateExponentialDoubleHistogramMetric(
                               UniqueName("histogram"), 0, 1.25, 3, 3),
                           -1.0, .1, 40.0));
  }

  loop.Run();

  return 0;
}
