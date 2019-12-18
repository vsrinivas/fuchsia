// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <zircon/status.h>

#include <zxtest/zxtest.h>

#include "../../uapp/psutils/ps_internal.h"

// Last character koid of deepest entry was getting dropped, see
// https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=7014#c48.
TEST(PrintTable, FullKoid) {
  char buf[4096];
  FILE* f = fmemopen(buf, sizeof(buf), "w");
  ASSERT_TRUE(f != NULL);

  ps_options_t options = {.also_show_threads = false, .only_show_jobs = false, .format_unit = 0};
  task_entry_t entries[] = {
      {
          .type = 'j',
          .koid_str = "123456",
          .parent_koid_str = "0",
          .depth = 0,
          .name = "root",
          .state_str = "",
          .pss_bytes = 1234 * 1024 * 1024,
          .private_bytes = 1234 * 1024 * 1024,
          .shared_bytes = 1234 * 1024 * 1024,
      },
      {
          .type = 'j',
          .koid_str = "7894567",
          .parent_koid_str = "123456",
          .depth = 1,
          .name = "my-job",
          .state_str = "",
          .pss_bytes = 1234 * 1024 * 1024,
          .private_bytes = 1234 * 1024 * 1024,
          .shared_bytes = 1234 * 1024 * 1024,
      },
      {
          .type = 'p',
          .koid_str = "123456789",
          .parent_koid_str = "7894567",
          .depth = 2,
          .name = "my-proc",
          .state_str = "",
          .pss_bytes = 1234 * 1024 * 1024,
          .private_bytes = 1234 * 1024 * 1024,
          .shared_bytes = 1234 * 1024 * 1024,
      },
  };
  task_table_t table = {.entries = entries, .num_entries = 3, .capacity = 3};
  print_table(&table, &options, f);
  fclose(f);
  EXPECT_EQ(strcmp(buf,
                   "TASK                 PSS PRIVATE  SHARED   STATE NAME\n"
                   "j: 123456          1234M   1234M                 root\n"
                   "  j: 7894567       1234M   1234M                 my-job\n"
                   "    p: 123456789   1234M   1234M   1234M         my-proc\n"
                   "TASK                 PSS PRIVATE  SHARED   STATE NAME\n"),
            0);
}

TEST(PsUtils, PrintAll) {
  ps_options_t options = {.also_show_threads = false, .only_show_jobs = false, .format_unit = 0};
  ASSERT_OK(show_all_jobs(&options));
}

TEST(PsUtils, PrintAllThreads) {
  ps_options_t options = {.also_show_threads = true, .only_show_jobs = false, .format_unit = 0};
  ASSERT_OK(show_all_jobs(&options));
}

TEST(PsUtils, PrintAllJobs) {
  ps_options_t options = {.also_show_threads = false, .only_show_jobs = true, .format_unit = 0};
  ASSERT_OK(show_all_jobs(&options));
}
