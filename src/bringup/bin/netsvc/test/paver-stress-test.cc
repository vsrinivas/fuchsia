// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/netboot.h"
#include "src/bringup/bin/netsvc/netsvc.h"
#include "src/bringup/bin/netsvc/test/paver-test-common.h"

extern "C" {
void update_timeouts() {}
bool netbootloader() { return false; }
bool all_features() { return true; }
const char* nodename() { return "test"; }
void netboot_run_cmd(const char* cmd) {}

void udp6_recv(void* data, size_t len, const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport) {}

void netifc_recv(void* data, size_t len) {}
bool netifc_send_pending() { return false; }
}

// We attempt to write more data than we have memory to ensure we are not keeping the file in memory
// the entire time.
TEST_F(PaverTest, WriteFvmManyLargeWrites) {
  constexpr size_t kChunkSize = 1 << 20;  // 1MiB
  auto fake_data = std::make_unique<uint8_t[]>(kChunkSize);
  memset(fake_data.get(), 0x4F, kChunkSize);

  const size_t payload_size = zx_system_get_physmem();

  fake_svc_.fake_paver().set_expected_payload_size(payload_size);
  fake_svc_.fake_paver().set_wait_for_start_signal(true);
  ASSERT_EQ(paver_.OpenWrite(NB_FVM_FILENAME, payload_size), TFTP_NO_ERROR);
  for (size_t offset = 0; offset < payload_size; offset += kChunkSize) {
    size_t size = std::min(kChunkSize, payload_size - offset);
    ASSERT_EQ(paver_.Write(fake_data.get(), &size, offset), TFTP_NO_ERROR);
    ASSERT_EQ(size, std::min(kChunkSize, payload_size - offset));
    // Stop and wait for all the data to be consumed, as we write data much faster than it can be
    // consumed.
    if ((offset / kChunkSize) % 100 == 0) {
      fake_svc_.fake_paver().WaitForWritten(offset);
    }
  }
  fake_svc_.fake_paver().WaitForWritten(payload_size + 1);
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());
  ASSERT_EQ(fake_svc_.fake_paver().GetCommandTrace(), std::vector<Command>{Command::kWriteVolumes});
}
