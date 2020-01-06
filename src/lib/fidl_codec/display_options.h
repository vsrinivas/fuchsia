// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_DISPLAY_OPTIONS_H_
#define SRC_LIB_FIDL_CODEC_DISPLAY_OPTIONS_H_

struct DisplayOptions {
  bool pretty_print = false;
  bool with_process_info = false;
  int columns = 0;
  bool needs_colors = false;
  bool dump_messages = false;
};

#endif  // SRC_LIB_FIDL_CODEC_DISPLAY_OPTIONS_H_
