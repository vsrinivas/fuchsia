// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_TESTS_TEST_NODE_SERVER_H_
#define LIB_ZXIO_TESTS_TEST_NODE_SERVER_H_

#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/fit/function.h>

namespace zxio_tests {

// Implementation of a fuchsia_io::Node server that implements Close() and
// creates a test failure for all other messages.
class CloseOnlyNodeServer : public fuchsia_io::testing::Node_TestBase {
  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE("unexpected message received: %s", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  // Exercised by |zxio_close|.
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) final {
    completer.Reply(ZX_OK);
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }

  // Exercised by |zxio_close|.
  void Close2(Close2RequestView request, Close2Completer::Sync& completer) final {
    completer.Reply({});
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }
};

// Implementation of a fuchsia_io::Node server that implements Close() and
// defers Describe() to a provided callback and creates a test failure for all
// other messages.
class DescribeNodeServer : public CloseOnlyNodeServer {
 public:
  using DescribeFunc =
      fit::function<void(DescribeRequestView request, DescribeCompleter::Sync& completer)>;

  void set_describe_function(DescribeFunc describe) { describe_ = std::move(describe); }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) final {
    describe_(std::move(request), completer);
  }

 private:
  DescribeFunc describe_;
};

}  // namespace zxio_tests

#endif  // LIB_ZXIO_TESTS_TEST_NODE_SERVER_H_
