// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/result.h>

#include <string>

#include <zxtest/zxtest.h>

TEST(Result, ReasonShouldNotBeUsedInOkResult) {
#ifdef __Fuchsia__
  fidl::Result ok_result = fidl::Result::Ok();
  ASSERT_DEATH(([&] { (void)ok_result.reason(); }), "reason");
#endif  // __Fuchsia__
}

// TODO(fxbug.dev/49971): We would be able to remove the differences between
// fuchsia/host if |zx_status_get_string| is available on host.
static std::string SelectErrorDescription(const std::string& fuchsia, const std::string& host) {
#ifdef __Fuchsia__
  return fuchsia;
#else
  return host;
#endif  // __Fuchsia__
}

TEST(Result, OkDescription) { ASSERT_EQ("FIDL success", fidl::Result::Ok().FormatDescription()); }

TEST(Result, UnboundDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL operation failed due to user initiated unbind, status: ZX_ERR_CANCELED (-23), "
      "detail: failed outgoing operation on unbound channel",
      "FIDL operation failed due to user initiated unbind, status: -23, "
      "detail: failed outgoing operation on unbound channel");
  ASSERT_EQ(expected, fidl::Result::Unbound().FormatDescription());
}

TEST(Result, UnknownOrdinalDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL operation failed due to unexpected message, status: ZX_ERR_NOT_SUPPORTED (-2), "
      "detail: unknown ordinal",
      "FIDL operation failed due to unexpected message, status: -2, "
      "detail: unknown ordinal");
  ASSERT_EQ(expected, fidl::Result::UnknownOrdinal().FormatDescription());
}

TEST(Result, TransportErrorDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL operation failed due to underlying transport I/O error, "
      "status: ZX_ERR_INVALID_ARGS (-10), detail: foo",
      "FIDL operation failed due to underlying transport I/O error, "
      "status: -10, detail: foo");
  ASSERT_EQ(expected, fidl::Result::TransportError(ZX_ERR_INVALID_ARGS, "foo").FormatDescription());
}

TEST(Result, PeerClosedDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL operation failed due to peer closed, status: ZX_ERR_PEER_CLOSED (-24)",
      "FIDL operation failed due to peer closed, status: -24");
  ASSERT_EQ(expected, fidl::Result::TransportError(ZX_ERR_PEER_CLOSED).FormatDescription());
}

TEST(Result, EncodeErrorDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL operation failed due to encode error, status: ZX_ERR_INVALID_ARGS (-10), "
      "detail: foo",
      "FIDL operation failed due to encode error, status: -10, detail: foo");
  ASSERT_EQ(expected, fidl::Result::EncodeError(ZX_ERR_INVALID_ARGS, "foo").FormatDescription());
}

TEST(Result, DecodeErrorDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL operation failed due to decode error, "
      "status: ZX_ERR_INVALID_ARGS (-10), detail: foo",
      "FIDL operation failed due to decode error, "
      "status: -10, detail: foo");
  ASSERT_EQ(expected, fidl::Result::DecodeError(ZX_ERR_INVALID_ARGS, "foo").FormatDescription());
}

TEST(Result, UnexpectedMessageDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL operation failed due to unexpected message, "
      "status: ZX_ERR_INVALID_ARGS (-10), detail: foo",
      "FIDL operation failed due to unexpected message, "
      "status: -10, detail: foo");
  ASSERT_EQ(expected,
            fidl::Result::UnexpectedMessage(ZX_ERR_INVALID_ARGS, "foo").FormatDescription());
}

TEST(UnbindInfo, UnbindDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL endpoint was unbound due to user initiated unbind, status: ZX_OK (0)",
      "FIDL endpoint was unbound due to user initiated unbind, status: 0");
  ASSERT_EQ(expected, fidl::UnbindInfo::Unbind().FormatDescription());
}

TEST(UnbindInfo, CloseDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL endpoint was unbound due to (server) user initiated close with epitaph, "
      "status of sending epitaph: ZX_ERR_INVALID_ARGS (-10)",
      "FIDL endpoint was unbound due to (server) user initiated close with epitaph, "
      "status of sending epitaph: -10");
  ASSERT_EQ(expected, fidl::UnbindInfo::Close(ZX_ERR_INVALID_ARGS).FormatDescription());
}

TEST(UnbindInfo, PeerClosedDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL endpoint was unbound due to peer closed, status: ZX_ERR_PEER_CLOSED (-24)",
      "FIDL endpoint was unbound due to peer closed, status: -24");
  ASSERT_EQ(expected, fidl::UnbindInfo::PeerClosed(ZX_ERR_PEER_CLOSED).FormatDescription());
}

TEST(UnbindInfo, PeerClosedEpitaphDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL endpoint was unbound due to peer closed, epitaph: ZX_ERR_INVALID_ARGS (-10)",
      "FIDL endpoint was unbound due to peer closed, epitaph: -10");
  ASSERT_EQ(expected, fidl::UnbindInfo::PeerClosed(ZX_ERR_INVALID_ARGS).FormatDescription());
}

TEST(UnbindInfo, DispatcherErrorDescription) {
  std::string expected = SelectErrorDescription(
      "FIDL endpoint was unbound due to dispatcher error, "
      "status: ZX_ERR_ACCESS_DENIED (-30)",
      "FIDL endpoint was unbound due to dispatcher error, status: -30");
  ASSERT_EQ(expected, fidl::UnbindInfo::DispatcherError(ZX_ERR_ACCESS_DENIED).FormatDescription());
}
