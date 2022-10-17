// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/file-api.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <memory>
#include <string_view>

#include <zxtest/zxtest.h>

namespace {

class FakePaver : public netsvc::PaverInterface {
 public:
  bool InProgress() override { return in_progress_; }
  zx_status_t exit_code() override { return exit_code_; }
  void reset_exit_code() override { exit_code_ = ZX_OK; }

  tftp_status OpenWrite(std::string_view filename, size_t size, zx::duration) override {
    in_progress_ = true;
    return TFTP_NO_ERROR;
  }
  tftp_status Write(const void* data, size_t* length, off_t offset) override {
    if (!in_progress_) {
      return TFTP_ERR_INTERNAL;
    }
    exit_code_ = ZX_OK;
    return TFTP_NO_ERROR;
  }
  void Close() override { in_progress_ = false; }
  void Abort() override { ADD_FATAL_FAILURE("unexpected call to abort"); }

  void set_exit_code(zx_status_t exit_code) { exit_code_ = exit_code; }

 private:
  bool in_progress_ = false;
  zx_status_t exit_code_ = ZX_OK;
};

constexpr char kReadData[] = "laksdfjsadfa";
constexpr char kFakeData[] = "lalala";

class FakeNetCopy : public netsvc::NetCopyInterface {
 public:
  int Open(const char* filename, uint32_t arg, size_t* file_size) override {
    if (arg == O_RDONLY) {
      *file_size = sizeof(kReadData);
    }
    return 0;
  }
  ssize_t Read(void* data_out, std::optional<off_t> offset, size_t max_len) override {
    const size_t len = std::min(sizeof(kReadData), max_len);
    memcpy(data_out, kReadData, len);
    return len;
  }
  ssize_t Write(const char* data, std::optional<off_t> offset, size_t length) override {
    return length;
  }
  int Close() override { return 0; }
  void AbortWrite() override {}
};

class FakeSysinfo : public fidl::WireServer<fuchsia_sysinfo::SysInfo> {
 public:
  FakeSysinfo(async_dispatcher_t* dispatcher) {
    zx::result server_end = fidl::CreateEndpoints(&svc_chan_);
    ASSERT_OK(server_end.status_value());
    fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end.value()), this);
  }

  void GetBoardName(GetBoardNameCompleter::Sync& completer) override {
    completer.Reply(ZX_OK, fidl::StringView{board_, sizeof(board_)});
  }

  void GetBoardRevision(GetBoardRevisionCompleter::Sync& completer) override {
    completer.Reply(ZX_OK, 0);
  }

  void GetBootloaderVendor(GetBootloaderVendorCompleter::Sync& completer) override {
    completer.Reply(ZX_OK, fidl::StringView{vendor_, sizeof(vendor_)});
  }

  void GetInterruptControllerInfo(GetInterruptControllerInfoCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
  }

  fidl::ClientEnd<fuchsia_sysinfo::SysInfo>& svc_chan() { return svc_chan_; }

  void set_board_name(const char* board) { strlcpy(board_, board, sizeof(board_)); }
  void set_bootloader_vendor(const char* vendor) { strlcpy(vendor_, vendor, sizeof(vendor_)); }

 private:
  fidl::ClientEnd<fuchsia_sysinfo::SysInfo> svc_chan_;

  char board_[32] = {};
  char vendor_[32] = {};
};

}  // namespace

class FileApiTest : public zxtest::Test {
 protected:
  FileApiTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        fake_sysinfo_(loop_.dispatcher()),
        file_api_(true, std::make_unique<FakeNetCopy>(), std::move(fake_sysinfo_.svc_chan()),
                  &fake_paver_) {
    loop_.StartThread("file-api-test-loop");
  }

  async::Loop loop_;
  FakePaver fake_paver_;
  FakeSysinfo fake_sysinfo_;
  netsvc::FileApi file_api_;
};

TEST_F(FileApiTest, OpenReadNetCopy) {
  ASSERT_EQ(file_api_.OpenRead("file", zx::duration::infinite()), sizeof(kReadData));
  file_api_.Close();
}

TEST_F(FileApiTest, OpenReadFailedPave) {
  fake_paver_.set_exit_code(ZX_ERR_INTERNAL);
  ASSERT_NE(file_api_.OpenRead("file", zx::duration::infinite()), sizeof(kReadData));
}

TEST_F(FileApiTest, OpenWriteNetCopy) {
  ASSERT_EQ(file_api_.OpenWrite("file", 10, zx::duration::infinite()), TFTP_NO_ERROR);
  file_api_.Close();
}

TEST_F(FileApiTest, OpenWriteBoardName) {
  ASSERT_EQ(file_api_.OpenWrite(NB_BOARD_NAME_FILENAME, 10, zx::duration::infinite()),
            TFTP_NO_ERROR);
  file_api_.Close();
}

TEST_F(FileApiTest, OpenWritePaver) {
  ASSERT_EQ(file_api_.OpenWrite(NB_IMAGE_PREFIX, 10, zx::duration::infinite()), TFTP_NO_ERROR);
  file_api_.Close();
}

TEST_F(FileApiTest, OpenWriteWhilePaving) {
  ASSERT_EQ(file_api_.OpenWrite(NB_IMAGE_PREFIX, 10, zx::duration::infinite()), TFTP_NO_ERROR);
  ASSERT_NE(file_api_.OpenWrite(NB_IMAGE_PREFIX, 10, zx::duration::infinite()), TFTP_NO_ERROR);
  file_api_.Close();
}

TEST_F(FileApiTest, OpenReadWhilePaving) {
  ASSERT_EQ(file_api_.OpenWrite(NB_IMAGE_PREFIX, 10, zx::duration::infinite()), TFTP_NO_ERROR);
  ASSERT_LT(file_api_.OpenRead("file", zx::duration::infinite()), 0);
  file_api_.Close();
}

TEST_F(FileApiTest, OpenWriteFailedPave) {
  fake_paver_.set_exit_code(ZX_ERR_INTERNAL);
  ASSERT_NE(file_api_.OpenWrite("file", 10, zx::duration::infinite()), TFTP_NO_ERROR);
}

TEST_F(FileApiTest, WriteNetCopy) {
  ASSERT_EQ(file_api_.OpenWrite("file", 10, zx::duration::infinite()), TFTP_NO_ERROR);
  size_t len = sizeof(kFakeData);
  ASSERT_EQ(file_api_.Write(kFakeData, &len, 0), TFTP_NO_ERROR);
  ASSERT_EQ(len, sizeof(kFakeData));
  file_api_.Close();
}

TEST_F(FileApiTest, WriteBoardName) {
  fake_sysinfo_.set_board_name(kFakeData);
  ASSERT_EQ(file_api_.OpenWrite(NB_BOARD_NAME_FILENAME, 10, zx::duration::infinite()),
            TFTP_NO_ERROR);
#if __x86_64__
  // We hardcode x64 to return "x64" no matter what sysinfo returns.
  constexpr char kBoardName[] = "x64";
  size_t len = sizeof(kBoardName);
  ASSERT_EQ(file_api_.Write(kBoardName, &len, 0), TFTP_NO_ERROR);
  ASSERT_EQ(len, sizeof(kBoardName));
#else
  size_t len = sizeof(kFakeData);
  ASSERT_EQ(file_api_.Write(kFakeData, &len, 0), TFTP_NO_ERROR);
  ASSERT_EQ(len, sizeof(kFakeData));
#endif
  file_api_.Close();
}

TEST_F(FileApiTest, WriteWrongBoardName) {
  fake_sysinfo_.set_board_name("other");
  ASSERT_EQ(file_api_.OpenWrite(NB_BOARD_NAME_FILENAME, 10, zx::duration::infinite()),
            TFTP_NO_ERROR);
  size_t len = sizeof(kFakeData);
  ASSERT_NE(file_api_.Write(kFakeData, &len, 0), TFTP_NO_ERROR);
  file_api_.Close();
}

TEST_F(FileApiTest, ReadBoardInfo) {
  fake_sysinfo_.set_board_name(kFakeData);
  board_info_t board_info = {};
  size_t len = sizeof(board_info);
  ASSERT_EQ(file_api_.OpenRead(NB_BOARD_INFO_FILENAME, zx::duration::infinite()), len);
  ASSERT_EQ(file_api_.Read(&board_info, &len, 0), TFTP_NO_ERROR);
  ASSERT_EQ(len, sizeof(board_info));
#if __x86_64__
  // We hardcode x64 to return "x64" no matter what sysinfo returns.
  constexpr char kBoardName[] = "x64";
  ASSERT_BYTES_EQ(board_info.board_name, kBoardName, sizeof(kBoardName));
#else
  ASSERT_BYTES_EQ(board_info.board_name, kFakeData, sizeof(kFakeData));
#endif
  file_api_.Close();
}

TEST_F(FileApiTest, WritePaver) {
  ASSERT_EQ(file_api_.OpenWrite(NB_IMAGE_PREFIX, 10, zx::duration::infinite()), TFTP_NO_ERROR);
  size_t len = sizeof(kFakeData);
  ASSERT_EQ(file_api_.Write(kFakeData, &len, 0), TFTP_NO_ERROR);
  ASSERT_EQ(len, sizeof(kFakeData));
  file_api_.Close();
}

TEST_F(FileApiTest, WriteAfterClose) {
  ASSERT_EQ(file_api_.OpenWrite("file", 10, zx::duration::infinite()), TFTP_NO_ERROR);
  file_api_.Close();
  size_t len = sizeof(kFakeData);
  ASSERT_NE(file_api_.Write(kFakeData, &len, 0), TFTP_NO_ERROR);
}

TEST_F(FileApiTest, WriteNoLength) {
  ASSERT_EQ(file_api_.OpenWrite(NB_IMAGE_PREFIX, 10, zx::duration::infinite()), TFTP_NO_ERROR);
  ASSERT_NE(file_api_.Write(kFakeData, nullptr, 0), TFTP_NO_ERROR);
  file_api_.Close();
}

TEST_F(FileApiTest, WriteWithoutOpen) {
  size_t len = sizeof(kFakeData);
  ASSERT_NE(file_api_.Write(kFakeData, &len, 0), TFTP_NO_ERROR);
}

TEST_F(FileApiTest, AbortNetCopyWrite) {
  ASSERT_EQ(file_api_.OpenWrite("file", 10, zx::duration::infinite()), TFTP_NO_ERROR);
  size_t len = sizeof(kFakeData);
  ASSERT_EQ(file_api_.Write(kFakeData, &len, 0), TFTP_NO_ERROR);
  ASSERT_EQ(len, sizeof(kFakeData));
  file_api_.Abort();
}
