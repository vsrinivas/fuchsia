// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/processing/coefficient_table.h"

namespace {

using ::media_audio::CoefficientTable;
using ::media_audio::Fixed;
using ::media_audio::SincFilterCoefficientTable;

struct Key {
  int32_t source_rate;
  int32_t dest_rate;

  SincFilterCoefficientTable::Inputs MakeInputs() {
    return SincFilterCoefficientTable::MakeInputs(source_rate, dest_rate);
  }
};

}  // namespace

int main(int argc, const char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: [program] [output_filename]\n";
    return 1;
  }

  std::ofstream out(argv[1]);

  out << "// Copyright 2021 The Fuchsia Authors. All rights reserved.\n";
  out << "// Use of this source code is governed by a BSD-style license that can be\n";
  out << "// found in the LICENSE file.\n";
  out << "//\n";
  out << "// Generated by gen_coefficient_tables.cc.\n";
  out << "\n";
  out << "#include \"src/media/audio/lib/processing/coefficient_table.h\"\n";
  out << "\n";
  out << "namespace media_audio {\n";
  out << "\n";
  out << "// Static asserts to validate that the generated code is not out-of-date\n";
  out << "static_assert(media_audio::kPtsFractionalBits == " << Fixed::Format::FractionalBits
      << ");\n";
  out << "static_assert(media_audio::SincFilterCoefficientTable::kSideTaps == "
      << SincFilterCoefficientTable::kSideTaps << ");\n";
  out << "static_assert(media_audio::SincFilterCoefficientTable::kFracSideLength == "
      << SincFilterCoefficientTable::kFracSideLength << ");\n";
  out << "\n";

  // TODO(fxbug.dev/86662): Move these to a shared header, to eliminate duplication with filter.cc.
  std::vector<Key> keys = {
      // clang-format off
      {48000, 48000},
      {96000, 48000},
      {48000, 96000},
      {96000, 16000},
      {48000, 16000},
      {44100, 48000},
      {16000, 48000},
      // clang-format on
  };

  std::vector<std::unique_ptr<CoefficientTable>> tables;
  for (auto key : keys) {
    tables.push_back(SincFilterCoefficientTable::Create(key.MakeInputs()));
  }

  // Print each table as an individual C array.
  for (size_t k = 0; k < tables.size(); k++) {
    out << "static const float kPrebuiltTableData" << k << "[] = {\n  ";
    size_t count = 0;
    for (auto x : tables[k]->raw_table()) {
      out << std::scientific << std::setprecision(std::numeric_limits<decltype(x)>::max_digits10)
          << x << "f, ";
      count++;
      if (count == tables[k]->raw_table().size()) {
        out << "\n";
      } else if (count % (SincFilterCoefficientTable::kSideTaps + 1) == 0) {
        out << "\n  ";
      }
    }
    out << "};\n";
    out << "\n";
  }

  // Print the collection of tables as an array of structs.
  out << "static const PrebuiltSincFilterCoefficientTable kPrebuiltTables[] = {\n";
  for (size_t k = 0; k < tables.size(); k++) {
    out << "  {\n";
    out << "    .source_rate = " << keys[k].source_rate << ",\n";
    out << "    .dest_rate = " << keys[k].dest_rate << ",\n";
    out << "    .table = cpp20::span<const float>(kPrebuiltTableData" << k
        << ", static_cast<size_t>(" << tables[k]->raw_table().size() << ")),\n";
    out << "  },\n";
  }
  out << "};\n";
  out << "\n";

  // Now translate to a span.
  out << "__attribute__((__visibility__(\"default\")))\n"
         "const cpp20::span<const PrebuiltSincFilterCoefficientTable> "
         "kPrebuiltSincFilterCoefficientTables(kPrebuiltTables, static_cast<size_t>("
      << tables.size() << "));\n";
  out << "\n";
  out << "}  // namespace media_audio\n";

  return 0;
}
