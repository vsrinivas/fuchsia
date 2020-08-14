// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_DISPLAY_OPTIONS_H_
#define SRC_LIB_FIDL_CODEC_DISPLAY_OPTIONS_H_

struct ExtraGeneration {
  enum class Kind { kSummary, kTop };
  Kind kind;
  std::string path;
  ExtraGeneration(Kind kind, std::string_view path) : kind(kind), path(path) {}
};

struct DisplayOptions {
  bool pretty_print = false;
  bool with_process_info = false;
  int columns = 0;
  bool needs_colors = false;
  bool extra_generation_needs_colors = false;
  std::vector<ExtraGeneration> extra_generation;
  bool dump_messages = false;

  void AddExtraGeneration(ExtraGeneration::Kind kind, std::string_view path) {
    extra_generation.emplace_back(kind, path);
  }
};

#endif  // SRC_LIB_FIDL_CODEC_DISPLAY_OPTIONS_H_
