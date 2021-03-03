// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdio.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <vector>

#include <zxtest/zxtest.h>

namespace sdio {

class SdioTest : public zxtest::Test, public ::fuchsia_hardware_sdio::Device::Interface {
 public:
  SdioTest() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {
    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &client_, &server));
    ASSERT_OK(fidl::BindSingleInFlightOnly<Device::Interface>(loop_.dispatcher(), std::move(server),
                                                              this));
    loop_.StartThread("sdio-test-loop");
  }

  void GetDevHwInfo(GetDevHwInfoCompleter::Sync& completer) override { completer.ReplySuccess({}); }

  void EnableFn(EnableFnCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void DisableFn(DisableFnCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void EnableFnIntr(EnableFnIntrCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void DisableFnIntr(DisableFnIntrCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void UpdateBlockSize(uint16_t blk_sz, bool deflt,
                       UpdateBlockSizeCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void GetBlockSize(GetBlockSizeCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void DoRwTxn(wire::SdioRwTxn txn, DoRwTxnCompleter::Sync& completer) override {
    txns_.push_back(wire::SdioRwTxn{
        .addr = txn.addr,
        .data_size = txn.data_size,
        .incr = txn.incr,
        .write = txn.write,
        .use_dma = txn.use_dma,
        .dma_vmo = {},
        .virt = {},
        .buf_offset = 0,
    });
    completer.ReplySuccess(std::move(txn));
  }

  void DoRwByte(bool write, uint32_t addr, uint8_t write_byte,
                DoRwByteCompleter::Sync& completer) override {
    if (write) {
      byte_ = write_byte;
    }

    address_ = addr;
    completer.ReplySuccess(write ? 0 : byte_);
  }

  void GetInBandIntr(GetInBandIntrCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void IoAbort(IoAbortCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void IntrPending(IntrPendingCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void DoVendorControlRwByte(bool write, uint8_t addr, uint8_t write_byte,
                             DoVendorControlRwByteCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

 protected:
  void set_byte(uint8_t byte) { byte_ = byte; }
  uint8_t get_byte() { return byte_; }
  uint32_t get_address() { return address_; }
  const std::vector<wire::SdioRwTxn>& get_txns() { return txns_; }

  void ExpectTxnsEqual(const wire::SdioRwTxn& lhs, const wire::SdioRwTxn& rhs) {
    EXPECT_EQ(lhs.addr, rhs.addr);
    EXPECT_EQ(lhs.data_size, rhs.data_size);
    EXPECT_EQ(lhs.incr, rhs.incr);
    EXPECT_EQ(lhs.write, rhs.write);
    EXPECT_EQ(lhs.use_dma, rhs.use_dma);
  }

  async::Loop loop_;
  zx::channel client_;

 private:
  uint8_t byte_ = 0;
  uint32_t address_ = 0;
  std::vector<wire::SdioRwTxn> txns_;
};

TEST_F(SdioTest, NoArguments) {
  EXPECT_NE(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 0, nullptr));
}

TEST_F(SdioTest, UnknownCommand) {
  const char* argv[] = {"bad"};
  EXPECT_NE(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 1, argv));
}

TEST_F(SdioTest, Info) {
  const char* argv[] = {"info"};
  EXPECT_EQ(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 1, argv));
}

TEST_F(SdioTest, ReadByte) {
  const char* argv[] = {"read-byte", "0x01234"};
  EXPECT_EQ(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 2, argv));
  EXPECT_EQ(get_address(), 0x01234);
}

TEST_F(SdioTest, ReadByteBadAddress) {
  const char* argv[] = {"read-byte", "0x123zz"};
  EXPECT_NE(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 2, argv));
}

TEST_F(SdioTest, WriteByte) {
  const char* argv[] = {"write-byte", "5000", "0xab"};
  EXPECT_EQ(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 3, argv));
  EXPECT_EQ(get_address(), 5000);
  EXPECT_EQ(get_byte(), 0xab);
}

TEST_F(SdioTest, WriteByteBadAddress) {
  const char* argv[] = {"write-byte", "-10", "0xab"};
  EXPECT_NE(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 3, argv));
}

TEST_F(SdioTest, WriteByteBadByte) {
  const char* argv[] = {"write-byte", "5000", "0x100"};
  EXPECT_NE(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 3, argv));
}

TEST_F(SdioTest, WriteByteNotEnoughArguments) {
  const char* argv[] = {"write-byte", "5000"};
  EXPECT_NE(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 2, argv));
}

TEST_F(SdioTest, ReadStress) {
  const char* argv[] = {"read-stress", "0x10000", "256", "20"};
  EXPECT_EQ(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 4, argv));
  EXPECT_EQ(get_txns().size(), 20);

  const wire::SdioRwTxn kExpectedTxn = {
      .addr = 0x10000,
      .data_size = 256,
      .incr = true,
      .write = false,
      .use_dma = false,
      .dma_vmo = {},
      .virt = {},
      .buf_offset = 0,
  };

  for (const wire::SdioRwTxn& txn : get_txns()) {
    ASSERT_NO_FATAL_FAILURES(ExpectTxnsEqual(txn, kExpectedTxn));
  }
}

TEST_F(SdioTest, ReadStressDma) {
  const char* argv[] = {"read-stress", "0x10000", "256", "20", "--dma"};
  EXPECT_EQ(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 5, argv));
  EXPECT_EQ(get_txns().size(), 20);

  const wire::SdioRwTxn kExpectedTxn = {
      .addr = 0x10000,
      .data_size = 256,
      .incr = true,
      .write = false,
      .use_dma = true,
      .dma_vmo = {},
      .virt = {},
      .buf_offset = 0,
  };

  for (const wire::SdioRwTxn& txn : get_txns()) {
    ASSERT_NO_FATAL_FAILURES(ExpectTxnsEqual(txn, kExpectedTxn));
  }
}

TEST_F(SdioTest, ReadStressFifo) {
  const char* argv[] = {"read-stress", "0x10000", "256", "20", "--fifo"};
  EXPECT_EQ(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 5, argv));
  EXPECT_EQ(get_txns().size(), 20);

  const wire::SdioRwTxn kExpectedTxn = {
      .addr = 0x10000,
      .data_size = 256,
      .incr = false,
      .write = false,
      .use_dma = false,
      .dma_vmo = {},
      .virt = {},
      .buf_offset = 0,
  };

  for (const wire::SdioRwTxn& txn : get_txns()) {
    ASSERT_NO_FATAL_FAILURES(ExpectTxnsEqual(txn, kExpectedTxn));
  }
}

TEST_F(SdioTest, ReadStressDmaFifo) {
  const char* argv[] = {"read-stress", "0x10000", "256", "20", "--dma", "--fifo"};
  EXPECT_EQ(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 6, argv));
  EXPECT_EQ(get_txns().size(), 20);

  const wire::SdioRwTxn kExpectedTxn = {
      .addr = 0x10000,
      .data_size = 256,
      .incr = false,
      .write = false,
      .use_dma = true,
      .dma_vmo = {},
      .virt = {},
      .buf_offset = 0,
  };

  for (const wire::SdioRwTxn& txn : get_txns()) {
    ASSERT_NO_FATAL_FAILURES(ExpectTxnsEqual(txn, kExpectedTxn));
  }
}

TEST_F(SdioTest, ReadStressFifoDma) {
  const char* argv[] = {"read-stress", "0x10000", "256", "20", "--fifo", "--dma"};
  EXPECT_EQ(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 6, argv));
  EXPECT_EQ(get_txns().size(), 20);

  const wire::SdioRwTxn kExpectedTxn = {
      .addr = 0x10000,
      .data_size = 256,
      .incr = false,
      .write = false,
      .use_dma = true,
      .dma_vmo = {},
      .virt = {},
      .buf_offset = 0,
  };

  for (const wire::SdioRwTxn& txn : get_txns()) {
    ASSERT_NO_FATAL_FAILURES(ExpectTxnsEqual(txn, kExpectedTxn));
  }
}

TEST_F(SdioTest, ReadStressBadAddress) {
  const char* argv[] = {"read-stress", "0x20000", "256", "20", "--fifo", "--dma"};
  EXPECT_NE(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 6, argv));
}

TEST_F(SdioTest, ReadStressNotEnoughArguments) {
  const char* argv[] = {"read-stress", "0x10000", "256"};
  EXPECT_NE(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 3, argv));
}

TEST_F(SdioTest, ReadStressSizeTooBig) {
  const char* argv[] = {"read-stress", "0x10000", "0x200001", "20"};
  EXPECT_NE(0, sdio::RunSdioTool(SdioClient(std::move(client_)), 4, argv));
}

TEST_F(SdioTest, GetTxnStats) {
  EXPECT_STR_EQ(GetTxnStats(zx::sec(2), 100).c_str(), "2.000 s (50.000 B/s)");
  EXPECT_STR_EQ(GetTxnStats(zx::msec(2), 100).c_str(), "2.000 ms (50.000 kB/s)");
  EXPECT_STR_EQ(GetTxnStats(zx::usec(2), 100).c_str(), "2.000 us (50.000 MB/s)");
  EXPECT_STR_EQ(GetTxnStats(zx::nsec(2), 100).c_str(), "2 ns (50.000 GB/s)");
  EXPECT_STR_EQ(GetTxnStats(zx::usec(-2), 100).c_str(), "-2000 ns (-50000000.000 B/s)");
  EXPECT_STR_EQ(GetTxnStats(zx::usec(2), 0).c_str(), "2.000 us (0.000 B/s)");
  EXPECT_STR_EQ(GetTxnStats(zx::nsec(0), 100).c_str(), "0 ns");
}

}  // namespace sdio
