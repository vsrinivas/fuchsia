// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_DISPLAY_OPTIONS_H_
#define TOOLS_FIDLCAT_LIB_DISPLAY_OPTIONS_H_

struct DisplayOptions {
  bool pretty_print = false;
  int columns = 0;
  bool needs_colors = false;
};

#endif  // TOOLS_FIDLCAT_LIB_DISPLAY_OPTIONS_H_
