// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client.h"

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace btlib {
namespace gatt {
namespace {

common::MutableByteBufferPtr NewPDU(size_t param_size) {
  auto pdu = common::NewSlabBuffer(sizeof(att::Header) + param_size);
  if (!pdu) {
    FXL_VLOG(1) << "att: Out of memory";
  }

  return pdu;
}

std::string ErrorToString(att::ErrorCode ecode) {
  switch (ecode) {
    case att::ErrorCode::kNoError:
      return "No Error";
    case att::ErrorCode::kInvalidHandle:
      return "Invalid Handle";
    case att::ErrorCode::kReadNotPermitted:
      return "Read Not Permitted";
    case att::ErrorCode::kWriteNotPermitted:
      return "Write Not Permitted";
    case att::ErrorCode::kInvalidPDU:
      return "Invalid PDU";
    case att::ErrorCode::kInsufficientAuthentication:
      return "Insuff. Authentication";
    case att::ErrorCode::kRequestNotSupported:
      return "Request Not Supported";
    case att::ErrorCode::kInvalidOffset:
      return "Invalid Offset";
    case att::ErrorCode::kInsufficientAuthorization:
      return "Insuff. Authorization";
    case att::ErrorCode::kPrepareQueueFull:
      return "Prepare Queue Full";
    case att::ErrorCode::kAttributeNotFound:
      return "Attribute Not Found";
    case att::ErrorCode::kAttributeNotLong:
      return "Attribute Not Long";
    case att::ErrorCode::kInsufficientEncryptionKeySize:
      return "Insuff. Encryption Key Size";
    case att::ErrorCode::kInvalidAttributeValueLength:
      return "Invalid Attribute Value Length";
    case att::ErrorCode::kUnlikelyError:
      return "Unlikely Error";
    case att::ErrorCode::kInsufficientEncryption:
      return "Insuff. Encryption";
    case att::ErrorCode::kUnsupportedGroupType:
      return "Unsupported Group Type";
    case att::ErrorCode::kInsufficientResources:
      return "Insuff. Resources";
    default:
      break;
  }

  return "(unknown)";
}

std::string FormatError(att::ErrorCode ecode) {
  return fxl::StringPrintf("%s (0x%02hhu)", ErrorToString(ecode).c_str(),
                           ecode);
}

}  // namespace

Client::Client(fxl::RefPtr<att::Bearer> bearer)
    : att_(bearer), weak_ptr_factory_(this) {
  FXL_DCHECK(att_);
}

void Client::ExchangeMTU(MTUCallback mtu_cb) {
  auto pdu = NewPDU(sizeof(att::ExchangeMTURequestParams));
  if (!pdu)
    return;

  att::PacketWriter writer(att::kExchangeMTURequest, pdu.get());
  auto params = writer.mutable_payload<att::ExchangeMTURequestParams>();
  params->client_rx_mtu = htole16(att_->preferred_mtu());

  auto rsp_cb = BindCallback([this, mtu_cb](const att::PacketReader& rsp) {
    FXL_DCHECK(rsp.opcode() == att::kExchangeMTUResponse);

    if (rsp.payload_size() != sizeof(att::ExchangeMTUResponseParams)) {
      // Received a malformed response. Disconnect the link.
      att_->ShutDown();

      // TODO(armansito): Use a host error code here.
      mtu_cb(att::ErrorCode::kInvalidPDU, 0);
      return;
    }

    const auto& rsp_params = rsp.payload<att::ExchangeMTUResponseParams>();
    uint16_t server_mtu = le16toh(rsp_params.server_rx_mtu);

    // If the minimum value is less than the default MTU, then go with the
    // default MTU (Vol 3, Part F, 3.4.2.2).
    uint16_t final_mtu =
        std::max(att::kLEMinMTU, std::min(server_mtu, att_->preferred_mtu()));
    att_->set_mtu(final_mtu);

    mtu_cb(att::ErrorCode::kNoError, final_mtu);
  });

  auto error_cb = BindErrorCallback([this, mtu_cb](bool timeout,
                                                   att::ErrorCode ecode,
                                                   att::Handle handle) {
    // "If the Error Response is sent by the server with the Error Code set to
    // Request Not Supported, [...] the default MTU shall be used (Vol 3, Part
    // G, 4.3.1)"
    if (ecode == att::ErrorCode::kRequestNotSupported) {
      FXL_VLOG(1) << "gatt: Peer does not support MTU exchange: using default";
      att_->set_mtu(att::kLEMinMTU);
      mtu_cb(att::ErrorCode::kNoError, att::kLEMinMTU);
      return;
    }

    FXL_VLOG(1) << "gatt: Exchange MTU failed: " << FormatError(ecode);
    mtu_cb(ecode, 0);
  });

  att_->StartTransaction(std::move(pdu), rsp_cb, error_cb);
}

att::Bearer::TransactionCallback Client::BindCallback(
    att::Bearer::TransactionCallback callback) {
  return [self = weak_ptr_factory_.GetWeakPtr(), callback](const auto& rsp) {
    if (self) {
      callback(rsp);
    }
  };
}

att::Bearer::ErrorCallback Client::BindErrorCallback(
    att::Bearer::ErrorCallback callback) {
  return [self = weak_ptr_factory_.GetWeakPtr(), callback](
             bool timeout, att::ErrorCode ecode, att::Handle handle) {
    if (self) {
      callback(timeout, ecode, handle);
    }
  };
}

}  // namespace gatt
}  // namespace btlib
