// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <trace-reader/file_reader.h>

#include "src/performance/memory/profile/fxt_to_pprof.h"

class FileRecordContainer : public RecordContainer {
 public:
  explicit FileRecordContainer(std::vector<std::string> fxt_paths)
      : fxt_paths_(std::move(fxt_paths)) {}
  bool ForEach(std::function<void(const trace::Record&)> record_consumer) const override {
    for (const auto& path : fxt_paths_) {
      std::unique_ptr<trace::FileReader> reader;
      if (!trace::FileReader::Create(
              path.c_str(), record_consumer,
              [](const fbl::String& error) { std::cout << "ERROR: error" << std::endl; },
              &reader)) {
        return false;
      }
      reader->ReadFile();
    }
    return true;
  }

 private:
  const std::vector<std::string> fxt_paths_;
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " PROFILE [... PROFILE]" << std::endl;
    return 1;
  }

  std::vector<std::string> fxt_paths;
  for (int i = 1; i < argc; i++) {
    fxt_paths.push_back(argv[i]);
  }

  const FileRecordContainer container(fxt_paths);
  auto pprof = fxt_to_profile(container, "memory_profile");
  if (pprof.is_error()) {
    std::cerr << std::endl << "Failed: " << pprof.error_value() << std::endl;
    return 2;
  }

  {
    std::string out_path(argv[1]);
    out_path += ".pb";
    std::cout << "Write pprof to " << out_path << std::endl;
    std::ofstream output(out_path.c_str(), std::ios::out);
    pprof.value().SerializePartialToOstream(&output);
  }
  return 0;
}
