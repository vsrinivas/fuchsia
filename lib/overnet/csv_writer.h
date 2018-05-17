// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace overnet {

class CsvWriter {
 public:
  CsvWriter() = default;
  CsvWriter(const CsvWriter&) = delete;
  CsvWriter& operator=(const CsvWriter&) = delete;

  CsvWriter& Put(const std::string& column, const std::string& value) {
    if (!in_row_) {
      rows_.emplace_back();
      in_row_ = true;
    }
    auto it = known_names_.find(column);
    if (it == known_names_.end()) {
      it = known_names_.emplace(column, known_names_.size()).first;
    }
    std::vector<std::string>& row = rows_.back();
    if (it->second >= row.size()) {
      row.resize(it->second + 1);
    }
    row[it->second] = value;
    return *this;
  }

  template <class T>
  CsvWriter& Put(const std::string& column, const T& value) {
    std::ostringstream out;
    out << value;
    return Put(column, out.str());
  }

  CsvWriter& Put(const std::string& column, bool value) {
    return Put(column, value ? "true" : "false");
  }

  CsvWriter& Put(const std::string& column, uint8_t value) {
    return Put(column, static_cast<int>(value));
  }

  void EndRow() { in_row_ = false; }

  void Flush(std::ostream& out) {
    std::vector<std::string> column_names;
    column_names.resize(known_names_.size());
    for (const auto& kn : known_names_) {
      column_names[kn.second] = kn.first;
    }
    auto output_row = [&out](const std::vector<std::string>& row) {
      bool first = true;
      for (const auto& field : row) {
        // TODO(ctiller): Handle escaping.
        if (!first) out << ",";
        first = false;
        out << field;
      }
      out << "\n";
    };
    output_row(column_names);
    for (const auto& row : rows_) {
      output_row(row);
    }
  }

 private:
  std::map<std::string, size_t> known_names_;
  std::vector<std::vector<std::string>> rows_;
  bool in_row_ = false;
};

}  // namespace overnet