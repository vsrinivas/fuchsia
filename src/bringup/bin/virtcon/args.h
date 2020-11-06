// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_VIRTCON_ARGS_H_
#define SRC_BRINGUP_BIN_VIRTCON_ARGS_H_

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/gfx-font-data/gfx-font-data.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <string>

#include <gfx/gfx.h>
#include <hid/hid.h>

#include "vc-colors.h"

struct Arguments {
  bool disable = false;
  bool keep_log_visible = false;
  bool repeat_keys = true;
  bool hide_on_boot = false;
  size_t shells = 0;
  const color_scheme_t* color_scheme = &color_schemes[kDefaultColorScheme];
  const gfx_font* font = &gfx_font_9x16;
  const keychar_t* keymap = qwerty_map;
  std::string command;
};

zx_status_t ParseArgs(llcpp::fuchsia::boot::Arguments::SyncClient& client, Arguments* out);

#endif  // SRC_BRINGUP_BIN_VIRTCON_ARGS_H_
