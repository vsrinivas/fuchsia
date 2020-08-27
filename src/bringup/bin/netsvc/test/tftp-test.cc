// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/tftp.h"

#include <inet6/netifc.h>
#include <zxtest/zxtest.h>

#include "src/bringup/bin/netsvc/file-api.h"
#include "src/bringup/bin/netsvc/netboot.h"

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

namespace {

class FakeFileApi : public netsvc::FileApiInterface {
 public:
  ssize_t OpenRead(const char* filename) override { return 10; }
  tftp_status OpenWrite(const char* filename, size_t size) override { return ZX_OK; }
  tftp_status Read(void* data, size_t* length, off_t offset) override { return ZX_OK; }
  tftp_status Write(const void* data, size_t* length, off_t offset) override { return ZX_OK; }
  void Close() override {}
  void Abort() override {}

  bool is_write() override { return false; }
  const char* filename() override { return "filename"; }
};

}  // namespace

extern netsvc::FileApiInterface* g_file_api;

class TftpTest : public zxtest::Test {
 protected:
  TftpTest() { g_file_api = &fake_file_api_; }

  ~TftpTest() { g_file_api = nullptr; }

  FakeFileApi fake_file_api_;
};

TEST_F(TftpTest, NextTimeout) { ASSERT_EQ(tftp_next_timeout(), ZX_TIME_INFINITE); }

TEST_F(TftpTest, HasPending) { ASSERT_FALSE(tftp_has_pending()); }

// TODO(surajmalhotra): Synthesize some tftp packets for additional tests.
