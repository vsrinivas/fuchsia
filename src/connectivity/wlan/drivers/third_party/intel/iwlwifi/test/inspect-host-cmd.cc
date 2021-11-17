// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Util functions to dump info/data for debug.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/inspect-host-cmd.h"

#include <stdint.h>
#include <stdio.h>
#include <zircon/syscalls.h>

#include <cstdio>
#include <sstream>
#include <string>

#define INSPECT_BUFSIZ 512

// Given a 'bitmap_value' and an array of bit definition, this function returns a joined string
//  which indicates asserted bits.
//
// The returned string will NOT contain the tailing newline character.
//
static std::string join_bitmap_string(const char** bit_defs, size_t bit_defs_len,
                                      uint64_t bitmap_value) {
  std::ostringstream out;
  bool first = true;

  for (size_t i = bit_defs_len; i > 0; i--) {
    if (bitmap_value & BIT(i - 1)) {
      out << (first ? "" : ",") << bit_defs[i - 1];
      first = false;
    }
  }

  return out.str();
}

// This function returns a multi-line string.
static std::string inspect_host_cmd_str(struct iwl_host_cmd* cmd) {
  const char* flags_defs[] = {
      // bit definition of 'enum CMD_MODE'
      "async",        "want_skb",        "send_in_rfkill", "high_prio",
      "send_in_idle", "make_trans_idle", "wake_up_trans",  "want_async_callback",
  };
  const char* dataflags_defs[] = {
      // bit definition of 'enum iwl_hcmd_dataflag'
      "nocopy",
      "dup",
  };

  constexpr size_t kMaxStrBufSize = 1024;
  char strbuf[kMaxStrBufSize];
  std::snprintf(strbuf, kMaxStrBufSize, "host_cmd id[0x%02x] flags[0x%x:%s]\n", cmd->id, cmd->flags,
                join_bitmap_string(flags_defs, ARRAY_SIZE(flags_defs), cmd->flags).c_str());
  std::string out(strbuf);

  for (size_t i = 0; i < ARRAY_SIZE(cmd->len); i++) {
    std::snprintf(
        strbuf, kMaxStrBufSize, "  [%zu] dataflags[0x%x:%s] len[%d]\n", i, cmd->dataflags[i],
        join_bitmap_string(dataflags_defs, ARRAY_SIZE(dataflags_defs), cmd->dataflags[i]).c_str(),
        cmd->len[i]);
    out += std::string(strbuf);
  }

  return out;
}

void inspect_host_cmd(const char* func_name, int line_no, struct iwl_host_cmd* cmd) {
  printf("%s():%d %s", func_name, line_no, inspect_host_cmd_str(cmd).c_str());
}
