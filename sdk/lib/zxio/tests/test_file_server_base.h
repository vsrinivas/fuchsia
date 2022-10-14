// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_TESTS_TEST_FILE_SERVER_BASE_H_
#define LIB_ZXIO_TESTS_TEST_FILE_SERVER_BASE_H_

#include <fidl/fuchsia.io/cpp/wire_test_base.h>

#include <zxtest/zxtest.h>

namespace zxio_tests {

// This is a test friendly implementation of a fuchsia_io::File server
// that simply returns ZX_ERR_NOT_SUPPORTED for every operation other than
// fuchsia.io.File/Close.
class TestFileServerBase : public fidl::testing::WireTestBase<fuchsia_io::File> {
 public:
  TestFileServerBase() = default;
  virtual ~TestFileServerBase() = default;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE() << "unexpected message received: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  // Exercised by |zxio_close|.
  void Close(CloseCompleter::Sync& completer) override {
    completer.ReplySuccess();
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }
};

// This implementation provides a simple Read() implementation.
class TestReadFileServer : public TestFileServerBase {
 public:
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) final {
    uint8_t read_data[sizeof(kTestData)];
    memcpy(read_data, kTestData, sizeof(kTestData));
    completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(read_data));
  }

  static constexpr char kTestData[] = "abcdef";
};

}  // namespace zxio_tests

#endif  // LIB_ZXIO_TESTS_TEST_FILE_SERVER_BASE_H_
