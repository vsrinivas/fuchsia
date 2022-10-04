// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_TESTS_TEST_SOCKET_SERVER_H_
#define LIB_ZXIO_TESTS_TEST_SOCKET_SERVER_H_

#include <fidl/fuchsia.posix.socket.packet/cpp/wire_test_base.h>
#include <fidl/fuchsia.posix.socket.raw/cpp/wire_test_base.h>
#include <fidl/fuchsia.posix.socket/cpp/wire_test_base.h>

#include <zxtest/zxtest.h>

namespace zxio_tests {

class DatagramSocketServer final
    : public fidl::testing::WireTestBase<fuchsia_posix_socket::DatagramSocket> {
 public:
  DatagramSocketServer() = default;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE() << "unexpected message received: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync& completer) override {
    completer.ReplySuccess();
    completer.Close(ZX_OK);
  }
};

class PacketSocketServer final
    : public fidl::testing::WireTestBase<fuchsia_posix_socket_packet::Socket> {
 public:
  PacketSocketServer() = default;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE() << "unexpected message received: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) final {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync& completer) override {
    completer.ReplySuccess();
    completer.Close(ZX_OK);
  }
};

class RawSocketServer final : public fidl::testing::WireTestBase<fuchsia_posix_socket_raw::Socket> {
 public:
  RawSocketServer() = default;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE() << "unexpected message received: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) final {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync& completer) override {
    completer.ReplySuccess();
    completer.Close(ZX_OK);
  }
};

class StreamSocketServer final
    : public fidl::testing::WireTestBase<fuchsia_posix_socket::StreamSocket> {
 public:
  StreamSocketServer() = default;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE() << "unexpected message received: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync& completer) override {
    completer.ReplySuccess();
    completer.Close(ZX_OK);
  }
};

class SynchronousDatagramSocketServer final
    : public fidl::testing::WireTestBase<fuchsia_posix_socket::SynchronousDatagramSocket> {
 public:
  SynchronousDatagramSocketServer() = default;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE() << "unexpected message received: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) final {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync& completer) final {
    completer.ReplySuccess();
    completer.Close(ZX_OK);
  }
};

}  // namespace zxio_tests

#endif  // LIB_ZXIO_TESTS_TEST_SOCKET_SERVER_H_
