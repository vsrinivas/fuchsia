// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "build/tools/json_merge/json_merge.h"

static void usage(const char* exe_name) {
  fprintf(
      stderr,
      "Usage: %s --input [infile] [--output outfile] [--minify]\n"
      "\n"
      "Merge one or more JSON files to a single JSON file.\n"
      "If any input is not a valid JSON, the merge operation will fail.\n"
      "Consequently you can \"merge\" one JSON file to perform validation.\n"
      "If any two inputs overlap in the top-level key space, the merge "
      "operation will fail.\n"
      "Optionally the merged output can be minified.\n"
      "Consequently you can \"merge\" one JSON file to perform "
      "minification.\n"
      "\n"
      "Example usages:\n"
      "%s --input in1.json --input in2.json            # merges to STDOUT\n"
      "%s --input in1.json --minify --output out.json  # minifies to out.json\n"
      "%s --help                                       # prints this message\n",
      exe_name, exe_name, exe_name, exe_name);
}

int main(int argc, char** argv) {
  std::vector<input_file> inputs;
  std::ofstream output_file;
  auto output_buf = std::cout.rdbuf();
  bool minify = false;

  static struct option long_options[] = {
      {"input", required_argument, 0, 'i'},
      {"output", required_argument, 0, 'o'},
      {"minify", no_argument, 0, 'm'},
      {"help", no_argument, 0, 'h'},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "iomh", long_options, nullptr)) != -1) {
    switch (opt) {
      case 'i': {
        auto input = std::make_unique<std::ifstream>(optarg);
        if (!input->is_open()) {
          fprintf(stderr, "Could not read from input file %s\n", optarg);
          return 1;
        }
        inputs.push_back({.name = optarg, .contents = std::move(input)});
        break;
      }

      case 'o':
        output_file.open(optarg);
        if (!output_file.is_open()) {
          fprintf(stderr, "Could not write to output file %s\n", optarg);
          return 1;
        }
        output_buf = output_file.rdbuf();
        break;

      case 'm':
        minify = true;
        break;

      case 'h':
        usage(argv[0]);
        return 0;
    }
  }

  std::ostream output(output_buf);
  return JSONMerge(inputs, output, std::cerr, minify);
}
