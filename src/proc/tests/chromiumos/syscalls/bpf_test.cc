// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <syscall.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>
#include <linux/bpf.h>

namespace {

void CheckMapInfo(int map_fd);

int bpf(int cmd, union bpf_attr attr) { return (int)syscall(__NR_bpf, cmd, &attr, sizeof(attr)); }

int CreateTestMap() {
  int map_fd = bpf(BPF_MAP_CREATE, (union bpf_attr){
                                       .map_type = BPF_MAP_TYPE_HASH,
                                       .key_size = sizeof(int),
                                       .value_size = sizeof(int),
                                       .max_entries = 10,
                                   });
  EXPECT_GE(map_fd, 0) << strerror(errno);

  CheckMapInfo(map_fd);
  return map_fd;
}

void CheckMapInfo(int map_fd) {
  struct bpf_map_info map_info;
  EXPECT_EQ(bpf(BPF_OBJ_GET_INFO_BY_FD,
                (union bpf_attr){
                    .info =
                        {
                            .bpf_fd = (unsigned)map_fd,
                            .info_len = sizeof(map_info),
                            .info = (uintptr_t)&map_info,
                        },
                }),
            0)
      << strerror(errno);
  EXPECT_EQ(map_info.type, BPF_MAP_TYPE_HASH);
  EXPECT_EQ(map_info.key_size, sizeof(int));
  EXPECT_EQ(map_info.value_size, sizeof(int));
  EXPECT_EQ(map_info.max_entries, 10u);
  EXPECT_EQ(map_info.map_flags, 0u);
}

TEST(BpfTest, Map) {
  int map_fd = CreateTestMap();

  EXPECT_EQ(bpf(BPF_MAP_UPDATE_ELEM,
                (union bpf_attr){
                    .map_fd = (unsigned)map_fd,
                    .key = (uintptr_t)(int[]){1},
                    .value = (uintptr_t)(int[]){2},
                }),
            0)
      << strerror(errno);
  EXPECT_EQ(bpf(BPF_MAP_UPDATE_ELEM,
                (union bpf_attr){
                    .map_fd = (unsigned)map_fd,
                    .key = (uintptr_t)(int[]){2},
                    .value = (uintptr_t)(int[]){3},
                }),
            0)
      << strerror(errno);

  std::vector<int> keys;
  int next_key;
  int *last_key = nullptr;
  for (;;) {
    int err = bpf(BPF_MAP_GET_NEXT_KEY, (union bpf_attr){
                                            .map_fd = (unsigned)map_fd,
                                            .key = (uintptr_t)last_key,
                                            .next_key = (uintptr_t)&next_key,
                                        });
    if (err < 0 && errno == ENOENT)
      break;
    ASSERT_GE(err, 0) << strerror(errno);
    keys.push_back(next_key);
    last_key = &next_key;
  }
  std::sort(keys.begin(), keys.end());
  EXPECT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0], 1);
  EXPECT_EQ(keys[1], 2);

  // BPF_MAP_LOOKUP_ELEM is not yet implemented

  CheckMapInfo(map_fd);
}

TEST(BpfTest, PinMap) {
  int map_fd = CreateTestMap();
  const char *pin_path = "/sys/fs/bpf/foo";
  // Hack for the starmium galaxy environment
  if (access("/sys/fs/bpf", F_OK) != 0) {
    pin_path = "/var/fs/bpf/foo";
  }

  unlink(pin_path);
  ASSERT_EQ(bpf(BPF_OBJ_PIN,
                (union bpf_attr){
                    .pathname = (uintptr_t)pin_path,
                    .bpf_fd = (unsigned)map_fd,
                }),
            0)
      << strerror(errno);
  EXPECT_EQ(access(pin_path, F_OK), 0) << strerror(errno);

  EXPECT_EQ(close(map_fd), 0);
  map_fd = bpf(BPF_OBJ_GET, (union bpf_attr){.pathname = (uintptr_t)pin_path});
  ASSERT_GE(map_fd, 0) << strerror(errno);
  CheckMapInfo(map_fd);
}

}  // namespace
