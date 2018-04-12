// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client.h"

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "lib/fxl/logging.h"

#include "gatt_defs.h"

namespace btlib {

using common::BufferView;
using common::HostError;

namespace gatt {
namespace {

common::MutableByteBufferPtr NewPDU(size_t param_size) {
  auto pdu = common::NewSlabBuffer(sizeof(att::Header) + param_size);
  if (!pdu) {
    FXL_VLOG(1) << "att: Out of memory";
  }

  return pdu;
}

}  // namespace

ServiceData::ServiceData(att::Handle start,
                         att::Handle end,
                         const common::UUID& type)
    : range_start(start), range_end(end), type(type) {}

class Impl final : public Client {
 public:
  explicit Impl(fxl::RefPtr<att::Bearer> bearer)
      : att_(bearer), weak_ptr_factory_(this) {
    FXL_DCHECK(att_);
  }

  ~Impl() override = default;

 private:
  fxl::WeakPtr<Client> AsWeakPtr() override {
    return weak_ptr_factory_.GetWeakPtr();
  }

  void ExchangeMTU(MTUCallback mtu_cb) override {
    auto pdu = NewPDU(sizeof(att::ExchangeMTURequestParams));
    if (!pdu) {
      mtu_cb(att::Status(HostError::kOutOfMemory), 0);
      return;
    }

    att::PacketWriter writer(att::kExchangeMTURequest, pdu.get());
    auto params = writer.mutable_payload<att::ExchangeMTURequestParams>();
    params->client_rx_mtu = htole16(att_->preferred_mtu());

    auto rsp_cb = BindCallback([this, mtu_cb](const att::PacketReader& rsp) {
      FXL_DCHECK(rsp.opcode() == att::kExchangeMTUResponse);

      if (rsp.payload_size() != sizeof(att::ExchangeMTUResponseParams)) {
        // Received a malformed response. Disconnect the link.
        att_->ShutDown();

        mtu_cb(att::Status(HostError::kPacketMalformed), 0);
        return;
      }

      const auto& rsp_params = rsp.payload<att::ExchangeMTUResponseParams>();
      uint16_t server_mtu = le16toh(rsp_params.server_rx_mtu);

      // If the minimum value is less than the default MTU, then go with the
      // default MTU (Vol 3, Part F, 3.4.2.2).
      uint16_t final_mtu =
          std::max(att::kLEMinMTU, std::min(server_mtu, att_->preferred_mtu()));
      att_->set_mtu(final_mtu);

      mtu_cb(att::Status(), final_mtu);
    });

    auto error_cb = BindErrorCallback(
        [this, mtu_cb](att::Status status, att::Handle handle) {
          // "If the Error Response is sent by the server with the Error Code
          // set to Request Not Supported, [...] the default MTU shall be used
          // (Vol 3, Part G, 4.3.1)"
          if (status.is_protocol_error() &&
              status.protocol_error() == att::ErrorCode::kRequestNotSupported) {
            FXL_VLOG(1)
                << "gatt: Peer does not support MTU exchange: using default";
            att_->set_mtu(att::kLEMinMTU);
            mtu_cb(status, att::kLEMinMTU);
            return;
          }

          FXL_VLOG(1) << "gatt: Exchange MTU failed: " << status.ToString();
          mtu_cb(status, 0);
        });

    att_->StartTransaction(std::move(pdu), rsp_cb, error_cb);
  }

  void DiscoverPrimaryServices(ServiceCallback svc_callback,
                               StatusCallback status_callback) override {
    DiscoverPrimaryServicesInternal(att::kHandleMin, att::kHandleMax,
                                    std::move(svc_callback),
                                    std::move(status_callback));
  }

  void DiscoverPrimaryServicesInternal(att::Handle start,
                                       att::Handle end,
                                       ServiceCallback svc_callback,
                                       StatusCallback status_callback) {
    auto pdu = NewPDU(sizeof(att::ReadByGroupTypeRequestParams16));
    if (!pdu) {
      status_callback(att::Status(HostError::kOutOfMemory));
      return;
    }

    att::PacketWriter writer(att::kReadByGroupTypeRequest, pdu.get());
    auto* params =
        writer.mutable_payload<att::ReadByGroupTypeRequestParams16>();
    params->start_handle = htole16(start);
    params->end_handle = htole16(end);
    params->type = htole16(types::kPrimaryService16);

    auto rsp_cb = BindCallback([this, svc_cb = std::move(svc_callback),
                                res_cb = status_callback](
                                   const att::PacketReader& rsp) mutable {
      FXL_DCHECK(rsp.opcode() == att::kReadByGroupTypeResponse);

      if (rsp.payload_size() < sizeof(att::ReadByGroupTypeResponseParams)) {
        // Received malformed response. Disconnect the link.
        FXL_VLOG(1) << "gatt: Received malformed Read By Group Type response";
        att_->ShutDown();
        res_cb(att::Status(HostError::kPacketMalformed));
        return;
      }

      const auto& rsp_params =
          rsp.payload<att::ReadByGroupTypeResponseParams>();
      uint8_t entry_length = rsp_params.length;

      // We expect the returned attribute value to be a 16-bit or 128-bit
      // service UUID.
      constexpr size_t kAttrDataSize16 =
          sizeof(att::AttributeGroupDataEntry) + sizeof(att::AttributeType16);
      constexpr size_t kAttrDataSize128 =
          sizeof(att::AttributeGroupDataEntry) + sizeof(att::AttributeType128);

      if (entry_length != kAttrDataSize16 && entry_length != kAttrDataSize128) {
        FXL_VLOG(1) << "gatt: Invalid attribute data length!";
        att_->ShutDown();
        res_cb(att::Status(HostError::kPacketMalformed));
        return;
      }

      BufferView attr_data_list(rsp_params.attribute_data_list,
                                rsp.payload_size() - 1);
      if (attr_data_list.size() % entry_length) {
        FXL_VLOG(1) << "gatt: Malformed attribute data list!";
        att_->ShutDown();
        res_cb(att::Status(HostError::kPacketMalformed));
        return;
      }

      att::Handle last_handle = att::kHandleMax;
      while (attr_data_list.size()) {
        const auto& entry = attr_data_list.As<att::AttributeGroupDataEntry>();

        ServiceData service;
        service.range_start = le16toh(entry.start_handle);
        service.range_end = le16toh(entry.group_end_handle);

        if (service.range_end < service.range_start) {
          FXL_VLOG(1) << "gatt: Received malformed service range values!";
          res_cb(att::Status(HostError::kPacketMalformed));
          return;
        }

        last_handle = service.range_end;

        BufferView value(entry.value, entry_length - (2 * sizeof(att::Handle)));

        // This must succeed as we have performed the appropriate checks above.
        __UNUSED bool result = common::UUID::FromBytes(value, &service.type);
        FXL_DCHECK(result);

        // Notify the handler.
        svc_cb(service);

        attr_data_list = attr_data_list.view(entry_length);
      }

      // The procedure is over if we have reached the end of the handle range.
      if (last_handle == att::kHandleMax) {
        res_cb(att::Status());
        return;
      }

      // Request the next batch.
      DiscoverPrimaryServicesInternal(last_handle + 1, att::kHandleMax,
                                      std::move(svc_cb), std::move(res_cb));
    });

    auto error_cb =
        BindErrorCallback([this, res_cb = status_callback](att::Status status,
                                                           att::Handle handle) {
          // An Error Response code of "Attribute Not Found" indicates the end
          // of the procedure (v5.0, Vol 3, Part G, 4.4.1).
          if (status.is_protocol_error() &&
              status.protocol_error() == att::ErrorCode::kAttributeNotFound) {
            res_cb(att::Status());
            return;
          }

          res_cb(status);
        });

    att_->StartTransaction(std::move(pdu), rsp_cb, error_cb);
  }

  // Wraps |callback| in a TransactionCallback that only runs if this Client is
  // still alive.
  att::Bearer::TransactionCallback BindCallback(
      att::Bearer::TransactionCallback callback) {
    return [self = weak_ptr_factory_.GetWeakPtr(), callback](const auto& rsp) {
      if (self) {
        callback(rsp);
      }
    };
  }

  // Wraps |callback| in a ErrorCallback that only runs if this Client is still
  // alive.
  att::Bearer::ErrorCallback BindErrorCallback(
      att::Bearer::ErrorCallback callback) {
    return [self = weak_ptr_factory_.GetWeakPtr(), callback](
               att::Status status, att::Handle handle) {
      if (self) {
        callback(status, handle);
      }
    };
  }
  fxl::RefPtr<att::Bearer> att_;

  fxl::WeakPtrFactory<Client> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Impl);
};

// static
std::unique_ptr<Client> Client::Create(fxl::RefPtr<att::Bearer> bearer) {
  return std::make_unique<Impl>(std::move(bearer));
}

}  // namespace gatt
}  // namespace btlib
