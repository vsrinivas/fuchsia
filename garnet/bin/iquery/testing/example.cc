// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/async-loop/cpp/loop.h"
#include "lib/component/cpp/exposed_object.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/strings/string_printf.h"

#include <vector>

// A single cell in the table.
// Cells expose a string property, an int metric, and a double metric.
class Cell : public component::ExposedObject {
 public:
  Cell(const std::string& name, int64_t value, double double_value)
      : ExposedObject(UniqueName("cell")) {
    object_dir().set_prop("name", name);
    object_dir().set_metric("value", component::IntMetric(value));
    object_dir().set_metric("double_value",
                            component::DoubleMetric(double_value));
  }
};

// A row in the table, contains cells.
class Row : public component::ExposedObject {
 public:
  Row() : ExposedObject(UniqueName("row")) {}

  Row(Row&&) = default;
  Row& operator=(Row&&) = default;

  Cell* AddCell(const std::string& name, int64_t value, double double_value) {
    // Construct a new cell in-place, and expose it as a child of this object in
    // the Inspect output.
    cells_.push_back(Cell(name, value, double_value));
    auto ret = &(*cells_.rbegin());
    add_child(ret);
    return ret;
  }

 private:
  std::vector<Cell> cells_;
};

// A table, contains rows.
class Table : public component::ExposedObject {
 public:
  Table() : ExposedObject(UniqueName("table")) {}
  Row* AddRow() {
    // Construct a new row in-place, and expose it as a child of this object in
    // the Inspect output.
    rows_.push_back(Row());
    auto ret = rows_.rbegin();
    ret->set_parent(object_dir());
    return &(*ret);
  }

 private:
  std::vector<Row> rows_;
};

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
  auto startup_context = component::StartupContext::CreateFromStartupInfo();

  Table table;
  const int row_count = atoi(rows.c_str());
  const int col_count = atoi(columns.c_str());
  const int total = row_count * col_count;

  int idx = 0;
  for (int i = 0; i < row_count; ++i) {
    Row* row = table.AddRow();
    for (int j = 0; j < col_count; ++j) {
      // name is "(row,col)", value is i*j, and double_value is percentage done.
      row->AddCell(fxl::StringPrintf("(%d,%d)", i, j), i * j,
                   100.0 * (++idx) / total);
    }
  }

  // Set a property directly on the table.
  table.object_dir().set_prop("object_name", "Example Table");
  table.object_dir().set_prop("binary_data",
                              std::string("\x20\x00\x11\x12\x05", 5));
  table.object_dir().set_prop(std::string("\x05\x01\x02", 3),
                              "The key of this value is a binary value.");
  table.object_dir().set_prop(std::string("\x05\x01\x02", 3),
                              std::string("\x01\x02", 2));

  // Finally, expose the table itself as an object in the top-level directory.
  // This appears under out/objects/ in the hub for this component.
  table.set_parent(*startup_context->outgoing().object_dir());

  loop.Run();

  return 0;
}
