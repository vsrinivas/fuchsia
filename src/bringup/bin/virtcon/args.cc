// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "args.h"

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/gfx-font-data/gfx-font-data.h>
#include <stdlib.h>
#include <zircon/types.h>

#include <cstring>

zx_status_t ParseArgs(llcpp::fuchsia::boot::Arguments::SyncClient& client, Arguments* out) {
  fidl::StringView string_keys[]{
      fidl::StringView{"virtcon.colorscheme"},
      fidl::StringView{"virtcon.font"},
      fidl::StringView{"virtcon.keymap"},
  };

  auto string_resp = client.GetStrings(fidl::unowned_vec(string_keys));
  if (string_resp.ok() && !string_resp->values[0].is_null()) {
    std::string colorvar(string_resp->values[0].data(), string_resp->values[0].size());
    out->color_scheme = string_to_color_scheme(colorvar.c_str());
  } else {
    out->color_scheme = &color_schemes[kDefaultColorScheme];
  }

  if (string_resp.ok() && !string_resp->values[1].is_null()) {
    std::string font(string_resp->values[1].data(), string_resp->values[1].size());
    if (!strcmp(font.c_str(), "9x16")) {
      out->font = &gfx_font_9x16;
    } else if (!strcmp(font.c_str(), "18x32")) {
      out->font = &gfx_font_18x32;
    } else {
      out->font = &gfx_font_9x16;
      printf("vc: no such font '%s'\n", font.c_str());
    }
  } else {
    out->font = &gfx_font_9x16;
  }

  if (string_resp.ok() && !string_resp->values[2].is_null()) {
    std::string keymap(string_resp->values[2].data(), string_resp->values[2].size());
    if (!strcmp(keymap.c_str(), "qwerty")) {
      out->keymap = qwerty_map;
    } else if (!strcmp(keymap.c_str(), "dvorak")) {
      out->keymap = dvorak_map;
    } else {
      out->keymap = qwerty_map;
      printf("vc: no such keymap '%s'\n", keymap.c_str());
    }
  } else {
    out->keymap = qwerty_map;
  }

  llcpp::fuchsia::boot::BoolPair bool_keys[]{
      {fidl::StringView{"virtcon.keep-log-visible"}, false},
      {fidl::StringView{"virtcon.disable"}, false},
      {fidl::StringView{"virtcon.keyrepeat"}, true},
      {fidl::StringView{"virtcon.hide-on-boot"}, false},
  };

  auto bool_resp = client.GetBools(fidl::unowned_vec(bool_keys));
  if (bool_resp.ok()) {
    out->keep_log_visible = bool_resp->values[0];
    out->disable = bool_resp->values[1];
    out->repeat_keys = bool_resp->values[2];
    out->hide_on_boot = bool_resp->values[3];
  }

  return ZX_OK;
}
