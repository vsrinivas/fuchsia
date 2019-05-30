// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <filesystem>
#include <fstream>
#include <iostream>

#include "src/developer/bugreport/bug_report_client.h"

int main() {
  // TODO(DX-1550): Command line option: Support loading other files.
  std::istream* file = &std::cin;

  // TODO(DX-1550): Command line option: Support specifying output directory.
  //                The client shouldn't be opinionated about the storage site,
  //                but rather get the caller to provide it.
  const std::filesystem::path output_path = "/tmp";

  auto targets = bugreport::HandleBugReport(output_path, file);
  if (!targets) {
    std::cerr << "Error processing input bug report. Exiting." << std::endl;
    exit(1);
  }

  // Report the success.
  std::cout << "Bug report processing successful." << std::endl;
  for (auto& target : *targets) {
    auto path = output_path / target.name;
    std::cout << "Exported " << path << std::endl;
  }
}
