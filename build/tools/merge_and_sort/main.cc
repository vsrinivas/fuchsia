// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void GetLines(std::istream* in, std::vector<std::string>* out_lines) {
  std::string line;
  while (std::getline(*in, line)) {
    out_lines->push_back(line);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr
        << "usage: " << argv[0] << " LIST OUTPUT DEPFILE\n"
        << "Reads LIST, which is a file containing one path per line.\n"
        << "Reads all the lines from those files, sorts them, and removes "
           "duplicates.\n"
        << "Writes the result to OUTPUT.\n"
        << "Writes a depfile to DEPFILE.\n";
    return 1;
  }

  const char* list_path = argv[1];
  const char* output_path = argv[2];
  const char* depfile_path = argv[3];

  std::ifstream list(list_path);
  if (!list.is_open()) {
    std::cerr << "error: Failed to open: " << list_path << std::endl;
    return 1;
  }

  std::vector<std::string> paths;
  GetLines(&list, &paths);
  list.close();

  std::ofstream depfile(depfile_path);
  if (!depfile.is_open()) {
    std::cerr << "error: Failed to open: " << depfile_path << std::endl;
    return 1;
  }

  depfile << output_path << ":";

  std::vector<std::string> items;
  for (const auto& path : paths) {
    std::ifstream sublist(path);
    if (!sublist.is_open()) {
      std::cerr << "error: Failed to open: " << path << std::endl;
      return 1;
    }
    depfile << " " << path;
    GetLines(&sublist, &items);
  }

  depfile << std::endl;
  depfile.close();

  std::sort(items.begin(), items.end());

  std::ofstream output(output_path);
  if (!output.is_open()) {
    std::cerr << "error: Failed to open: " << output_path << std::endl;
    return 1;
  }

  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0 && items[i] == items[i - 1]) {
      continue;
    }
    output << items[i] << std::endl;
  }
  output.close();

  return 0;
}
