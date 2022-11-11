// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/fidl.serversuite/cpp/natural_types.h"
#include "lib/fidl/cpp/unified_messaging.h"
#include "src/tests/fidl/server_suite/harness/harness.h"
#include "src/tests/fidl/server_suite/harness/ordinals.h"

using namespace channel_util;

namespace server_suite {

const fidl_xunion_tag_t kResultUnionSuccess = 1;
const fidl_xunion_tag_t kResultUnionError = 2;
const fidl_xunion_tag_t kResultUnionTransportError = 3;

OPEN_SERVER_TEST(SendStrictEvent) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalSendEvent, fidl::MessageDynamicFlags::kStrictMethod),
      encode(fidl_serversuite::OpenTargetSendEventRequest(fidl_serversuite::EventType::kStrict)),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kOneWayTxid, kOrdinalStrictEvent, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(SendFlexibleEvent) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalSendEvent, fidl::MessageDynamicFlags::kStrictMethod),
      encode(fidl_serversuite::OpenTargetSendEventRequest(fidl_serversuite::EventType::kFlexible)),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kOneWayTxid, kOrdinalFlexibleEvent, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(ReceiveStrictOneWay) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalStrictOneWay, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  WAIT_UNTIL([this]() { return reporter().received_strict_one_way(); });
}

OPEN_SERVER_TEST(ReceiveStrictOneWayMismatchedStrictness) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalStrictOneWay, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  WAIT_UNTIL([this]() { return reporter().received_strict_one_way(); });
}

OPEN_SERVER_TEST(ReceiveFlexibleOneWay) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalFlexibleOneWay, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  WAIT_UNTIL([this]() { return reporter().received_flexible_one_way(); });
}

OPEN_SERVER_TEST(ReceiveFlexibleOneWayMismatchedStrictness) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalFlexibleOneWay, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  WAIT_UNTIL([this]() { return reporter().received_flexible_one_way(); });
}

OPEN_SERVER_TEST(StrictTwoWayResponse) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalStrictTwoWay, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalStrictTwoWay, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(StrictTwoWayResponseMismatchedStrictness) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalStrictTwoWay, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalStrictTwoWay, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(StrictTwoWayNonEmptyResponse) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalStrictTwoWayFields, fidl::MessageDynamicFlags::kStrictMethod),
      encode(fidl_serversuite::OpenTargetStrictTwoWayFieldsRequest(504230)),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalStrictTwoWayFields, fidl::MessageDynamicFlags::kStrictMethod),
      encode(fidl_serversuite::OpenTargetStrictTwoWayFieldsResponse(504230)),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(StrictTwoWayErrorSyntaxResponse) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalStrictTwoWayErr, fidl::MessageDynamicFlags::kStrictMethod),
      encode(fidl_serversuite::OpenTargetStrictTwoWayErrRequest::WithReplySuccess({})),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalStrictTwoWayErr, fidl::MessageDynamicFlags::kStrictMethod),
      union_ordinal(kResultUnionSuccess),
      inline_envelope(
          {
              padding(4),
          },
          false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(StrictTwoWayErrorSyntaxResponseMismatchedStrictness) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalStrictTwoWayErr, fidl::MessageDynamicFlags::kFlexibleMethod),
      encode(fidl_serversuite::OpenTargetStrictTwoWayErrRequest::WithReplySuccess({})),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalStrictTwoWayErr, fidl::MessageDynamicFlags::kStrictMethod),
      union_ordinal(kResultUnionSuccess),
      inline_envelope(
          {
              padding(4),
          },
          false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(StrictTwoWayErrorSyntaxNonEmptyResponse) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalStrictTwoWayFieldsErr, fidl::MessageDynamicFlags::kStrictMethod),
      encode(fidl_serversuite::OpenTargetStrictTwoWayFieldsErrRequest::WithReplySuccess(406601)),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalStrictTwoWayFieldsErr, fidl::MessageDynamicFlags::kStrictMethod),
      union_ordinal(kResultUnionSuccess),
      inline_envelope({i32(406601)}, false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(FlexibleTwoWayResponse) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWay, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWay, fidl::MessageDynamicFlags::kFlexibleMethod),
      union_ordinal(kResultUnionSuccess),
      inline_envelope({padding(4)}, false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(FlexibleTwoWayResponseMismatchedStrictness) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWay, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWay, fidl::MessageDynamicFlags::kFlexibleMethod),
      union_ordinal(kResultUnionSuccess),
      inline_envelope({padding(4)}, false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(FlexibleTwoWayNonEmptyResponse) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWayFields, fidl::MessageDynamicFlags::kFlexibleMethod),
      encode(fidl_serversuite::OpenTargetFlexibleTwoWayFieldsRequest(3023950)),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWayFields, fidl::MessageDynamicFlags::kFlexibleMethod),
      union_ordinal(kResultUnionSuccess),
      inline_envelope({i32(3023950)}, false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(FlexibleTwoWayErrorSyntaxResponseSuccessResult) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWayErr, fidl::MessageDynamicFlags::kFlexibleMethod),
      encode(fidl_serversuite::OpenTargetFlexibleTwoWayErrRequest::WithReplySuccess({})),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWayErr, fidl::MessageDynamicFlags::kFlexibleMethod),
      union_ordinal(kResultUnionSuccess),
      inline_envelope({padding(4)}, false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(FlexibleTwoWayErrorSyntaxResponseErrorResult) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWayErr, fidl::MessageDynamicFlags::kFlexibleMethod),
      encode(fidl_serversuite::OpenTargetFlexibleTwoWayErrRequest::WithReplyError(60602293)),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWayErr, fidl::MessageDynamicFlags::kFlexibleMethod),
      union_ordinal(kResultUnionError),
      inline_envelope({i32(60602293)}, false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(FlexibleTwoWayErrorSyntaxNonEmptyResponseSuccessResult) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWayFieldsErr,
             fidl::MessageDynamicFlags::kFlexibleMethod),
      encode(fidl_serversuite::OpenTargetFlexibleTwoWayFieldsErrRequest::WithReplySuccess(406601)),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWayFieldsErr,
             fidl::MessageDynamicFlags::kFlexibleMethod),
      union_ordinal(kResultUnionSuccess),
      inline_envelope({i32(406601)}, false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(FlexibleTwoWayErrorSyntaxNonEmptyResponseErrorResult) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWayFieldsErr,
             fidl::MessageDynamicFlags::kFlexibleMethod),
      encode(fidl_serversuite::OpenTargetFlexibleTwoWayFieldsErrRequest::WithReplyError(60602293)),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalFlexibleTwoWayFieldsErr,
             fidl::MessageDynamicFlags::kFlexibleMethod),
      union_ordinal(kResultUnionError),
      inline_envelope({i32(60602293)}, false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));
}

OPEN_SERVER_TEST(UnknownStrictOneWayOpenProtocol) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
  ASSERT_FALSE(reporter().received_unknown_method());
}

OPEN_SERVER_TEST(UnknownFlexibleOneWayOpenProtocol) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  WAIT_UNTIL([this]() { return reporter().received_unknown_method().has_value(); });

  ASSERT_EQ(kOrdinalFakeUnknownMethod, reporter().received_unknown_method()->ordinal());
  ASSERT_EQ(fidl_serversuite::UnknownMethodType::kOneWay,
            reporter().received_unknown_method()->unknown_method_type());

  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_PEER_CLOSED));
}

OPEN_SERVER_TEST(UnknownFlexibleOneWayHandleOpenProtocol) {
  zx::eventpair event1, event2;
  ASSERT_OK(zx::eventpair::create(0, &event1, &event2));

  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
      handle_present(),
      padding(4),
  };
  HandleDispositions hd_in = {
      zx_handle_disposition_t{
          .handle = event1.release(),
          .type = ZX_OBJ_TYPE_EVENTPAIR,
          .rights = ZX_RIGHT_SAME_RIGHTS,
      },
  };
  ASSERT_OK(client_end().write(bytes_in, hd_in));

  WAIT_UNTIL([this]() { return reporter().received_unknown_method().has_value(); });

  ASSERT_EQ(kOrdinalFakeUnknownMethod, reporter().received_unknown_method()->ordinal());
  ASSERT_EQ(fidl_serversuite::UnknownMethodType::kOneWay,
            reporter().received_unknown_method()->unknown_method_type());

  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_PEER_CLOSED));

  ASSERT_OK(event2.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

OPEN_SERVER_TEST(UnknownStrictTwoWayOpenProtocol) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
  ASSERT_FALSE(reporter().received_unknown_method());
}

OPEN_SERVER_TEST(UnknownFlexibleTwoWayOpenProtocol) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
      union_ordinal(kResultUnionTransportError),
      inline_envelope(
          {
              transport_err_unknown_method(),
          },
          false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));

  WAIT_UNTIL([this]() { return reporter().received_unknown_method().has_value(); });

  ASSERT_EQ(kOrdinalFakeUnknownMethod, reporter().received_unknown_method()->ordinal());
  ASSERT_EQ(fidl_serversuite::UnknownMethodType::kTwoWay,
            reporter().received_unknown_method()->unknown_method_type());

  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_PEER_CLOSED));
}

OPEN_SERVER_TEST(UnknownFlexibleTwoWayHandleOpenProtocol) {
  zx::eventpair event1, event2;
  ASSERT_OK(zx::eventpair::create(0, &event1, &event2));

  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
      handle_present(),
      padding(4),
  };
  HandleDispositions hd_in = {
      zx_handle_disposition_t{
          .handle = event1.release(),
          .type = ZX_OBJ_TYPE_EVENTPAIR,
          .rights = ZX_RIGHT_SAME_RIGHTS,
      },
  };
  ASSERT_OK(client_end().write(bytes_in, hd_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));

  Bytes bytes_out = {
      header(kTwoWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
      union_ordinal(kResultUnionTransportError),
      inline_envelope(
          {
              transport_err_unknown_method(),
          },
          false),
  };
  ASSERT_OK(client_end().read_and_check(bytes_out));

  WAIT_UNTIL([this]() { return reporter().received_unknown_method().has_value(); });

  ASSERT_EQ(kOrdinalFakeUnknownMethod, reporter().received_unknown_method()->ordinal());
  ASSERT_EQ(fidl_serversuite::UnknownMethodType::kTwoWay,
            reporter().received_unknown_method()->unknown_method_type());

  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_PEER_CLOSED));

  ASSERT_OK(event2.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), nullptr));
}

AJAR_SERVER_TEST(UnknownStrictOneWayAjarProtocol) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
  ASSERT_FALSE(reporter().received_unknown_method());
}

AJAR_SERVER_TEST(UnknownFlexibleOneWayAjarProtocol) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  WAIT_UNTIL([this]() { return reporter().received_unknown_method().has_value(); });

  ASSERT_EQ(kOrdinalFakeUnknownMethod, reporter().received_unknown_method()->ordinal());
  ASSERT_EQ(fidl_serversuite::UnknownMethodType::kOneWay,
            reporter().received_unknown_method()->unknown_method_type());

  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_PEER_CLOSED));
}

AJAR_SERVER_TEST(UnknownStrictTwoWayAjarProtocol) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
  ASSERT_FALSE(reporter().received_unknown_method());
}

AJAR_SERVER_TEST(UnknownFlexibleTwoWayAjarProtocol) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
  ASSERT_FALSE(reporter().received_unknown_method());
}

CLOSED_SERVER_TEST(UnknownStrictOneWayClosedProtocol) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
  ASSERT_FALSE(reporter().received_unknown_method());
}

CLOSED_SERVER_TEST(UnknownFlexibleOneWayClosedProtocol) {
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
  ASSERT_FALSE(reporter().received_unknown_method());
}

CLOSED_SERVER_TEST(UnknownStrictTwoWayClosedProtocol) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kStrictMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
  ASSERT_FALSE(reporter().received_unknown_method());
}

CLOSED_SERVER_TEST(UnknownFlexibleTwoWayClosedProtocol) {
  Bytes bytes_in = {
      header(kTwoWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
  };
  ASSERT_OK(client_end().write(bytes_in));

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_PEER_CLOSED));
  ASSERT_FALSE(client_end().is_signal_present(ZX_CHANNEL_READABLE));
  ASSERT_FALSE(reporter().received_unknown_method());
}

}  // namespace server_suite
