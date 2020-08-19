// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <zircon/status.h>

#include <zxtest/zxtest.h>

#include "src/sys/bin/psutils/handles-internal.h"

namespace {

constexpr zx_info_handle_extended_t handles[] = {
    {.type = ZX_OBJ_TYPE_THREAD,
     .handle_value = 0x1112311,
     .rights = ZX_DEFAULT_THREAD_RIGHTS,
     .koid = 150001,
     .related_koid = 1000,
     .peer_owner_koid = 0},
    {.type = ZX_OBJ_TYPE_CHANNEL,
     .handle_value = 0x3112431,
     .rights = ZX_DEFAULT_CHANNEL_RIGHTS,
     .koid = 150007,
     .related_koid = 150008,
     .peer_owner_koid = 0},
    {.type = ZX_OBJ_TYPE_EVENT,
     .handle_value = 0x12222,
     .rights = ZX_DEFAULT_EVENT_RIGHTS,
     .koid = 1025,
     .related_koid = 0,
     .peer_owner_koid = 0},
};

}  // namespace

TEST(PsUtilsHandles, NullInput) {
  char buf[256] = {};
  FILE* f = fmemopen(buf, sizeof(buf), "w");
  ASSERT_TRUE(f != NULL);

  const std::vector<zx_info_handle_extended_t> handles;
  auto printed = print_handles(f, handles, kAll);
  fclose(f);

  ASSERT_EQ(printed, 0u);
  ASSERT_EQ(buf[0], 0);
}

TEST(PsUtilsHandles, BasicOutput) {
  char buf[1024] = {};
  FILE* f = fmemopen(buf, sizeof(buf), "w");
  ASSERT_TRUE(f != NULL);

  std::vector<zx_info_handle_extended_t> vh(&handles[0], &handles[3]);

  auto printed = print_handles(f, vh, kAll);
  fclose(f);

  EXPECT_EQ(strcmp(buf,
                   "    handle    koid  rkoid     rights type\n"
                   "0x01112311: 150001   1000 0x0004d2cf thread\n"
                   "0x03112431: 150007 150008 0x0000f00e channel\n"
                   "0x00012222:   1025        0x0000d003 event\n"
                   "3 handles\n"),
            0);

  ASSERT_EQ(printed, 3u);
}

TEST(PsUtilsHandles, FilteredOutput) {
  char buf[1024] = {};
  FILE* f = fmemopen(buf, sizeof(buf), "w");
  ASSERT_TRUE(f != NULL);

  std::vector<zx_info_handle_extended_t> vh(&handles[0], &handles[3]);

  auto printed = print_handles(f, vh, kEvent);
  fclose(f);

  EXPECT_EQ(strcmp(buf,
                   "    handle  koid       rights type\n"
                   "0x00012222: 1025   0x0000d003 event\n"
                   "1 handles\n"),
            0);

  ASSERT_EQ(printed, 1u);
}
