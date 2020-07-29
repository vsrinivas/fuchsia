// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/rndis-function/rndis_function.h"

#include <zircon/status.h>

#include <algorithm>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddktl/protocol/usb/function.h>
#include <usb/request-cpp.h>

#include "src/connectivity/ethernet/drivers/rndis-function/rndis_function_bind.h"
#include "src/connectivity/ethernet/lib/rndis/rndis.h"

size_t RndisFunction::UsbFunctionInterfaceGetDescriptorsSize() { return sizeof(descriptors_); }

void RndisFunction::UsbFunctionInterfaceGetDescriptors(void* out_descriptors_buffer,
                                                       size_t descriptors_size,
                                                       size_t* out_descriptors_actual) {
  memcpy(out_descriptors_buffer, &descriptors_,
         std::min(descriptors_size, UsbFunctionInterfaceGetDescriptorsSize()));
  *out_descriptors_actual = UsbFunctionInterfaceGetDescriptorsSize();
}

std::optional<std::vector<uint8_t>> RndisFunction::QueryOid(uint32_t oid, void* input,
                                                            size_t length) {
  std::optional<std::vector<uint8_t>> response;
  switch (oid) {
    case OID_802_3_PERMANENT_ADDRESS: {
      std::vector<uint8_t> buffer;
      buffer.insert(buffer.end(), mac_addr_.begin(), mac_addr_.end());

      // Make the host and device addresses different so packets are routed correctly.
      buffer[5] ^= 1;

      response.emplace(buffer);
      break;
    }
    default:
      zxlogf(WARNING, "Unhandled OID query %x", oid);
      break;
  }
  return response;
}

zx_status_t RndisFunction::SetOid(uint32_t oid, const uint8_t* buffer, size_t length) {
  switch (oid) {
    case OID_GEN_CURRENT_PACKET_FILTER: {
      fbl::AutoLock lock(&lock_);
      rndis_ready_ = true;
      if (ifc_.is_valid()) {
        ifc_.Status(ETHERNET_STATUS_ONLINE);
      }

      std::optional<usb::Request<>> pending_request;
      size_t request_length = usb::Request<>::RequestSize(usb_request_size_);
      while ((pending_request = free_read_pool_.Get(request_length))) {
        function_.RequestQueue(pending_request->take(), &read_request_complete_);
      }

      return ZX_OK;
    }
    default:
      zxlogf(WARNING, "Unhandled OID: %x", oid);
      return ZX_ERR_NOT_SUPPORTED;
  }
}

std::vector<uint8_t> InvalidMessageResponse(const void* invalid_data, size_t size) {
  zxlogf(WARNING, "Host sent an invalid message.");

  std::vector<uint8_t> buffer(sizeof(rndis_indicate_status) + sizeof(rndis_diagnostic_info) + size);

  rndis_indicate_status status{
      .msg_type = RNDIS_INDICATE_STATUS_MSG,
      .msg_length = static_cast<uint32_t>(buffer.size()),
      .status = RNDIS_STATUS_INVALID_DATA,
      .status_buffer_length = static_cast<uint32_t>(size),
      .status_buffer_offset = static_cast<uint32_t>(sizeof(rndis_indicate_status) -
                                                    offsetof(rndis_indicate_status, status)),
  };

  rndis_diagnostic_info info{
      .diagnostic_status = RNDIS_STATUS_INVALID_DATA,
      // TODO: This is supposed to an offset to the error in |invalid_data|.
      .error_offset = 0,
  };

  memcpy(buffer.data(), &status, sizeof(status));
  uintptr_t offset = sizeof(status);
  memcpy(buffer.data() + offset, &info, sizeof(info));
  offset += sizeof(info);
  memcpy(buffer.data() + offset, invalid_data, size);

  return buffer;
}

std::vector<uint8_t> InitResponse(uint32_t request_id, uint32_t status) {
  rndis_init_complete response{.msg_type = RNDIS_INITIALIZE_CMPLT,
                               .msg_length = sizeof(rndis_init_complete),
                               .request_id = request_id,
                               .status = status,
                               .major_version = RNDIS_MAJOR_VERSION,
                               .minor_version = RNDIS_MINOR_VERSION,
                               .device_flags = RNDIS_DF_CONNECTIONLESS,
                               .medium = RNDIS_MEDIUM_802_3,
                               .max_packets_per_xfer = 1,
                               .max_xfer_size = RNDIS_MAX_XFER_SIZE,
                               .packet_alignment = 0,
                               .reserved0 = 0,
                               .reserved1 = 0};

  std::vector<uint8_t> buffer(sizeof(rndis_init_complete));
  memcpy(buffer.data(), &response, sizeof(rndis_init_complete));
  return buffer;
}

std::vector<uint8_t> QueryResponse(uint32_t request_id,
                                   const std::optional<std::vector<uint8_t>>& oid_response) {
  size_t buffer_size = sizeof(rndis_query_complete);
  if (oid_response.has_value()) {
    buffer_size += oid_response->size();
  }
  std::vector<uint8_t> buffer(buffer_size);

  rndis_query_complete response;
  response.msg_type = RNDIS_QUERY_CMPLT;
  response.msg_length = static_cast<uint32_t>(buffer.size());
  response.request_id = request_id;

  if (oid_response.has_value()) {
    response.status = RNDIS_STATUS_SUCCESS;
    response.info_buffer_offset =
        sizeof(rndis_query_complete) - offsetof(rndis_query_complete, request_id);
    response.info_buffer_length = static_cast<uint32_t>(oid_response->size());

    memcpy(buffer.data() + sizeof(rndis_query_complete), oid_response->data(),
           oid_response->size());
  } else {
    response.status = RNDIS_STATUS_NOT_SUPPORTED;
    response.info_buffer_offset = 0;
    response.info_buffer_length = 0;
  }

  memcpy(buffer.data(), &response, sizeof(rndis_query_complete));

  return buffer;
}

std::vector<uint8_t> SetResponse(uint32_t request_id, uint32_t status) {
  rndis_set_complete response{
      .msg_type = RNDIS_SET_CMPLT,
      .msg_length = static_cast<uint32_t>(sizeof(rndis_set_complete)),
      .request_id = request_id,
      .status = status,
  };

  std::vector<uint8_t> buffer(sizeof(rndis_set_complete));
  memcpy(buffer.data(), &response, sizeof(rndis_set_complete));
  return buffer;
}

zx_status_t RndisFunction::HandleCommand(const void* buffer, size_t size) {
  if (size < sizeof(rndis_header)) {
    fbl::AutoLock lock(&lock_);
    control_responses_.push(InvalidMessageResponse(buffer, size));
    NotifyLocked();
    return ZX_OK;
  }

  auto header = static_cast<const rndis_header*>(buffer);
  std::optional<std::vector<uint8_t>> response;

  switch (header->msg_type) {
    case RNDIS_INITIALIZE_MSG: {
      if (size < sizeof(rndis_init)) {
        response.emplace(InvalidMessageResponse(buffer, size));
        break;
      }

      auto init = static_cast<const rndis_init*>(buffer);
      if (init->major_version != RNDIS_MAJOR_VERSION) {
        zxlogf(WARNING, "Invalid RNDIS major version. Expected %x, got %x.", RNDIS_MAJOR_VERSION,
               init->major_version);
        response.emplace(InitResponse(init->request_id, RNDIS_STATUS_NOT_SUPPORTED));
      } else if (init->minor_version != RNDIS_MINOR_VERSION) {
        zxlogf(WARNING, "Invalid RNDIS minor version. Expected %x, got %x.", RNDIS_MINOR_VERSION,
               init->minor_version);
        response.emplace(InitResponse(init->request_id, RNDIS_STATUS_NOT_SUPPORTED));
      }

      response.emplace(InitResponse(init->request_id, RNDIS_STATUS_SUCCESS));
      break;
    }
    case RNDIS_QUERY_MSG: {
      if (size < sizeof(rndis_query)) {
        response.emplace(InvalidMessageResponse(buffer, size));
        break;
      }

      auto query = static_cast<const rndis_query*>(buffer);
      auto oid_response = QueryOid(query->oid, nullptr, 0);
      response.emplace(QueryResponse(query->request_id, oid_response));
      break;
    }
    case RNDIS_SET_MSG: {
      if (size < sizeof(rndis_set)) {
        response.emplace(InvalidMessageResponse(buffer, size));
        break;
      }

      auto set = static_cast<const rndis_set*>(buffer);
      if (set->info_buffer_length > RNDIS_SET_INFO_BUFFER_LENGTH) {
        response.emplace(SetResponse(set->request_id, RNDIS_STATUS_INVALID_DATA));
        break;
      }

      size_t offset = offsetof(rndis_set, request_id) + set->info_buffer_offset;
      if (offset + set->info_buffer_length > size) {
        response.emplace(SetResponse(set->request_id, RNDIS_STATUS_INVALID_DATA));
        break;
      }

      zx_status_t status = SetOid(set->oid, reinterpret_cast<const uint8_t*>(buffer) + offset,
                                  set->info_buffer_length);

      uint32_t rndis_status = RNDIS_STATUS_SUCCESS;
      if (status == ZX_ERR_NOT_SUPPORTED) {
        rndis_status = RNDIS_STATUS_NOT_SUPPORTED;
      } else if (status != ZX_OK) {
        rndis_status = RNDIS_STATUS_FAILURE;
      }
      response.emplace(SetResponse(set->request_id, rndis_status));
      break;
    }
    case RNDIS_HALT_MSG:
      zxlogf(WARNING, "Host sent a halt message, which we do not support yet.");
      break;
    case RNDIS_RESET_MSG:
      zxlogf(WARNING, "Host sent a reset message, which we do not support yet.");
      break;
    case RNDIS_PACKET_MSG:
      // The should only send packets on the data channel.
      // TODO: How should we respond to this?
      zxlogf(WARNING, "Host sent a data packet on the control channel.");
      break;
    default:
      response.emplace(InvalidMessageResponse(buffer, size));
      break;
  }

  if (!response.has_value()) {
    zxlogf(ERROR, "Reached bottom of HandleCommand without generating a response.");
    return ZX_ERR_INTERNAL;
  }
  fbl::AutoLock lock(&lock_);
  control_responses_.push(std::move(response.value()));
  NotifyLocked();
  return ZX_OK;
}

zx_status_t ErrorResponse(void* buffer, size_t size, size_t* actual) {
  if (size < 1) {
    *actual = 0;
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  // From
  // https://docs.microsoft.com/en-au/windows-hardware/drivers/network/control-channel-characteristics:
  // If for some reason the device receives a GET_ENCAPSULATED_RESPONSE and is unable to respond
  // with a valid data on the Control endpoint, then it should return a one-byte packet set to
  // 0x00, rather than stalling the Control endpoint.
  memset(buffer, 0x00, 1);
  *actual = 1;
  return ZX_OK;
}

zx_status_t RndisFunction::HandleResponse(void* buffer, size_t size, size_t* actual) {
  fbl::AutoLock lock(&lock_);
  if (control_responses_.empty()) {
    zxlogf(WARNING, "Host tried to read a control response when none was available.");
    return ErrorResponse(buffer, size, actual);
  }

  auto packet = control_responses_.front();
  if (size < packet.size()) {
    zxlogf(WARNING,
           "Buffer too small to read a control response. Packet size is %zd but the buffer is %zd.",
           packet.size(), size);
    return ErrorResponse(buffer, size, actual);
  }

  memcpy(buffer, packet.data(), packet.size());
  *actual = packet.size();

  control_responses_.pop();
  return ZX_OK;
}

zx_status_t RndisFunction::UsbFunctionInterfaceControl(const usb_setup_t* setup,
                                                       const void* write_buffer, size_t write_size,
                                                       void* out_read_buffer, size_t read_size,
                                                       size_t* out_read_actual) {
  if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
      setup->bRequest == USB_CDC_SEND_ENCAPSULATED_COMMAND) {
    if (out_read_actual) {
      *out_read_actual = 0;
    }
    zx_status_t status = HandleCommand(write_buffer, write_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Error handling command: %s", zx_status_get_string(status));
      return status;
    }
    return ZX_OK;
  } else if (setup->bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
             setup->bRequest == USB_CDC_GET_ENCAPSULATED_RESPONSE) {
    size_t actual;
    zx_status_t status = HandleResponse(out_read_buffer, read_size, &actual);
    if (out_read_actual) {
      *out_read_actual = actual;
    }
    return status;
  }

  zxlogf(WARNING, "Unrecognised control interface transfer: bmRequestType %x bRequest %x",
         setup->bmRequestType, setup->bRequest);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t RndisFunction::UsbFunctionInterfaceSetConfigured(bool configured, usb_speed_t speed) {
  if (configured) {
    zx_status_t status = function_.ConfigEp(&descriptors_.notification_ep, nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to configure control endpoint: %s", zx_status_get_string(status));
      return status;
    }

    status = function_.ConfigEp(&descriptors_.in_ep, nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to configure bulk in endpoint: %s", zx_status_get_string(status));
      return status;
    }
    status = function_.ConfigEp(&descriptors_.out_ep, nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to configure bulk out endpoint: %s", zx_status_get_string(status));
      return status;
    }
  } else {
    zx_status_t status = function_.DisableEp(NotificationAddress());
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to disable control endpoint: %s", zx_status_get_string(status));
      return status;
    }
    status = function_.DisableEp(BulkInAddress());
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to disable data in endpoint: %s", zx_status_get_string(status));
      return status;
    }
    status = function_.DisableEp(BulkOutAddress());
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to disable data out endpoint: %s", zx_status_get_string(status));
      return status;
    }
    fbl::AutoLock lock(&lock_);
    rndis_ready_ = false;
    if (ifc_.is_valid()) {
      ifc_.Status(0);
    }
  }
  return ZX_OK;
}

zx_status_t RndisFunction::UsbFunctionInterfaceSetInterface(uint8_t interface,
                                                            uint8_t alt_setting) {
  return ZX_OK;
}

zx_status_t RndisFunction::EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
  if (options) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (info) {
    *info = {};
    info->mtu = kMtu - sizeof(rndis_packet_header);
    memcpy(info->mac, mac_addr_.data(), mac_addr_.size());
    info->netbuf_size = eth::BorrowedOperation<>::OperationSize(sizeof(ethernet_netbuf_t));
  }

  return ZX_OK;
}

void RndisFunction::EthernetImplStop() {
  fbl::AutoLock lock(&lock_);
  ifc_.clear();
}

zx_status_t RndisFunction::EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&lock_);
  if (ifc_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  ifc_ = ddk::EthernetIfcProtocolClient(ifc);
  ifc_.Status(Online() ? ETHERNET_STATUS_ONLINE : 0);
  return ZX_OK;
}

void RndisFunction::EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                        ethernet_impl_queue_tx_callback completion_cb,
                                        void* cookie) {
  eth::BorrowedOperation<> op(netbuf, completion_cb, cookie, sizeof(ethernet_netbuf_t));

  size_t length = op.operation()->data_size;
  if (length > kMtu - sizeof(rndis_packet_header)) {
    zxlogf(ERROR, "Unsupported packet length %zu", length);
    op.Complete(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock lock(&lock_);

  if (!Online()) {
    op.Complete(ZX_ERR_SHOULD_WAIT);
    return;
  }

  std::optional<usb::Request<>> request;
  request = free_write_pool_.Get(usb::Request<>::RequestSize(usb_request_size_));

  if (!request) {
    zxlogf(DEBUG, "No available TX requests");
    op.Complete(ZX_ERR_SHOULD_WAIT);
    return;
  }

  rndis_packet_header header{};
  header.msg_type = RNDIS_PACKET_MSG;
  header.msg_length = static_cast<uint32_t>(sizeof(header) + length);
  header.data_offset = sizeof(header) - offsetof(rndis_packet_header, data_offset);
  header.data_length = static_cast<uint32_t>(length);

  size_t offset = 0;
  ssize_t copied = request->CopyTo(&header, sizeof(header), 0);
  if (copied < 0) {
    zxlogf(ERROR, "Failed to copy TX header: %zd", copied);
    op.Complete(ZX_ERR_INTERNAL);
    return;
  }
  offset += copied;

  request->CopyTo(op.operation()->data_buffer, length, offset);
  if (copied < 0) {
    zxlogf(ERROR, "Failed to copy TX data: %zd", copied);
    op.Complete(ZX_ERR_INTERNAL);
    return;
  }
  request->request()->header.length = sizeof(header) + length;

  function_.RequestQueue(request->take(), &write_request_complete_);
  op.Complete(ZX_OK);
}

zx_status_t RndisFunction::EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                                size_t data_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

void RndisFunction::ReceiveLocked(usb::Request<>& request) {
  auto& response = request.request()->response;

  uint8_t* data;
  zx_status_t status = request.Mmap(reinterpret_cast<void**>(&data));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to map RX data: %s", zx_status_get_string(status));
    return;
  }

  size_t remaining = response.actual;
  while (remaining >= sizeof(rndis_packet_header)) {
    const auto* header = reinterpret_cast<const rndis_packet_header*>(data);
    if (header->msg_type != RNDIS_PACKET_MSG) {
      zxlogf(WARNING, "Received invalid packet type %x.", header->msg_type);
      zxlogf(WARNING, "header length %lu.", request.request()->header.length);
      zxlogf(WARNING, "actual size %lu.", response.actual);
      zxlogf(WARNING, "header->msg_length %u.", header->msg_length);
      zxlogf(WARNING, "header->data_offset %x.", header->data_offset);
      return;
    }
    if (header->msg_length > remaining) {
      zxlogf(WARNING, "Received packet with invalid length %u: only %zd bytes left in frame.",
             header->msg_length, remaining);
      return;
    }
    if (header->msg_length < sizeof(rndis_packet_header)) {
      zxlogf(WARNING, "Received packet with invalid length %u: less than header length.",
             header->msg_length);
      return;
    }
    if (header->data_offset > header->msg_length - offsetof(rndis_packet_header, data_offset) ||
        header->data_length >
            header->msg_length - offsetof(rndis_packet_header, data_offset) - header->data_offset) {
      zxlogf(WARNING, "Received packet with invalid data.");
      return;
    }

    size_t offset = offsetof(rndis_packet_header, data_offset) + header->data_offset;
    ifc_.Recv(data + offset, header->data_length, /*flags=*/0);

    if (header->oob_data_offset != 0) {
      zxlogf(WARNING, "Packet contained unsupported out of band data.");
    }
    if (header->per_packet_info_offset != 0) {
      zxlogf(WARNING, "Packet contained unsupported per packet information.");
    }

    data = data + header->msg_length;
    remaining -= header->msg_length;
  }
}

void RndisFunction::ReadComplete(usb_request_t* usb_request) {
  fbl::AutoLock lock(&lock_);
  usb::Request<> request(usb_request, usb_request_size_);

  if (usb_request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    free_read_pool_.Add(std::move(request));
    return;
  }

  if (usb_request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(ERROR, "ReadComplete refused");
  } else if (usb_request->response.status != ZX_OK) {
    zxlogf(ERROR, "ReadComplete not ok");
  } else if (ifc_.is_valid()) {
    ReceiveLocked(request);
  }

  if (Online()) {
    function_.RequestQueue(request.take(), &read_request_complete_);
  } else {
    free_read_pool_.Add(std::move(request));
  }
}

void RndisFunction::NotifyLocked() {
  std::optional<usb::Request<>> request;
  request = free_notify_pool_.Get(usb::Request<>::RequestSize(usb_request_size_));

  if (!request) {
    zxlogf(ERROR, "No notify request available");
    return;
  }

  rndis_notification notification{
      .notification = htole32(1),
      .reserved = 0,
  };

  ssize_t copied = request->CopyTo(&notification, sizeof(notification), 0);
  if (copied < 0) {
    zxlogf(ERROR, "Failed to copy notification");
    return;
  }
  request->request()->header.length = sizeof(notification);

  function_.RequestQueue(request->take(), &notification_request_complete_);
}

void RndisFunction::WriteComplete(usb_request_t* usb_request) {
  fbl::AutoLock lock(&lock_);
  usb::Request<> request(usb_request, usb_request_size_);
  free_write_pool_.Add(std::move(request));
}

void RndisFunction::NotificationComplete(usb_request_t* usb_request) {
  fbl::AutoLock lock(&lock_);
  usb::Request<> request(usb_request, usb_request_size_);
  free_notify_pool_.Add(std::move(request));
}

zx_status_t RndisFunction::Bind() {
  descriptors_.communication_interface = usb_interface_descriptor_t{
      .bLength = sizeof(usb_interface_descriptor_t),
      .bDescriptorType = USB_DT_INTERFACE,
      .bInterfaceNumber = 0,  // set later
      .bAlternateSetting = 0,
      .bNumEndpoints = 1,
      .bInterfaceClass = USB_CLASS_COMM,
      .bInterfaceSubClass = 0x02,  // USB_SUBCLASS_MSC_RNDIS,
      .bInterfaceProtocol = 0xFF,
      .iInterface = 0,
  };
  descriptors_.cdc_header =
      usb_cs_header_interface_descriptor_t{
          .bLength = sizeof(usb_cs_header_interface_descriptor_t),
          .bDescriptorType = USB_DT_CS_INTERFACE,
          .bDescriptorSubType = USB_CDC_DST_HEADER,
          .bcdCDC = htole16(0x0110),
      },
  descriptors_.call_mgmt =
      usb_cs_call_mgmt_interface_descriptor_t{
          .bLength = sizeof(usb_cs_call_mgmt_interface_descriptor_t),
          .bDescriptorType = USB_DT_CS_INTERFACE,
          .bDescriptorSubType = USB_CDC_DST_CALL_MGMT,
          .bmCapabilities = 0x00,
          .bDataInterface = 0x01,
      },
  descriptors_.acm = usb_cs_abstract_ctrl_mgmt_interface_descriptor_t{
      .bLength = sizeof(usb_cs_abstract_ctrl_mgmt_interface_descriptor_t),
      .bDescriptorType = USB_DT_CS_INTERFACE,
      .bDescriptorSubType = USB_CDC_DST_ABSTRACT_CTRL_MGMT,
      .bmCapabilities = 0,
  };
  descriptors_.cdc_union = usb_cs_union_interface_descriptor_1_t{
      .bLength = sizeof(usb_cs_union_interface_descriptor_1_t),
      .bDescriptorType = USB_DT_CS_INTERFACE,
      .bDescriptorSubType = USB_CDC_DST_UNION,
      .bControlInterface = 0,      // set later
      .bSubordinateInterface = 0,  // set later
  };
  descriptors_.notification_ep = usb_endpoint_descriptor_t{
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = 0,  // set later
      .bmAttributes = USB_ENDPOINT_INTERRUPT,
      .wMaxPacketSize = htole16(kNotificationMaxPacketSize),
      .bInterval = 1,
  };

  descriptors_.data_interface = usb_interface_descriptor_t{
      .bLength = sizeof(usb_interface_descriptor_t),
      .bDescriptorType = USB_DT_INTERFACE,
      .bInterfaceNumber = 0,  // set later
      .bAlternateSetting = 0,
      .bNumEndpoints = 2,
      .bInterfaceClass = USB_CLASS_CDC,
      .bInterfaceSubClass = 0x02,  // USB_SUBCLASS_MSC_RNDIS,
      .bInterfaceProtocol = 0,
      .iInterface = 0,
  };
  descriptors_.in_ep = usb_endpoint_descriptor_t{
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = 0,  // set later
      .bmAttributes = USB_ENDPOINT_BULK,
      .wMaxPacketSize = htole16(512),
      .bInterval = 0,
  };
  descriptors_.out_ep = usb_endpoint_descriptor_t{
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = 0,  // set later
      .bmAttributes = USB_ENDPOINT_BULK,
      .wMaxPacketSize = htole16(512),
      .bInterval = 0,
  };

  zx_status_t status =
      function_.AllocInterface(&descriptors_.communication_interface.bInterfaceNumber);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to allocate communication interface: %s", zx_status_get_string(status));
    return status;
  }

  status = function_.AllocInterface(&descriptors_.data_interface.bInterfaceNumber);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to allocate data interface: %s", zx_status_get_string(status));
    return status;
  }

  descriptors_.cdc_union.bControlInterface = descriptors_.communication_interface.bInterfaceNumber;
  descriptors_.cdc_union.bSubordinateInterface = descriptors_.data_interface.bInterfaceNumber;

  status = function_.AllocEp(USB_DIR_OUT, &descriptors_.out_ep.bEndpointAddress);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to allocate bulk out interface: %s", zx_status_get_string(status));
    return status;
  }

  status = function_.AllocEp(USB_DIR_IN, &descriptors_.in_ep.bEndpointAddress);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to allocate bulk in interface: %s", zx_status_get_string(status));
    return status;
  }

  status = function_.AllocEp(USB_DIR_IN, &descriptors_.notification_ep.bEndpointAddress);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to allocate notification interface: %s", zx_status_get_string(status));
    return status;
  }

  size_t actual;
  status = DdkGetMetadata(DEVICE_METADATA_MAC_ADDRESS, mac_addr_.data(), mac_addr_.size(), &actual);
  if (status != ZX_OK || actual != mac_addr_.size()) {
    zxlogf(WARNING, "CDC: MAC address metadata not found. Generating random address");

    zx_cprng_draw(mac_addr_.data(), mac_addr_.size());
    mac_addr_[0] = 0x02;
  }

  zxlogf(INFO, "MAC address: %02x:%02x:%02x:%02x:%02x:%02x", mac_addr_[0], mac_addr_[1],
         mac_addr_[2], mac_addr_[3], mac_addr_[4], mac_addr_[5]);

  usb_request_size_ = function_.GetRequestSize();

  for (size_t i = 0; i < kRequestPoolSize; i++) {
    std::optional<usb::Request<>> request;
    status = usb::Request<>::Alloc(&request, kNotificationMaxPacketSize, NotificationAddress(),
                                   usb_request_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Allocating notify request failed %d", status);
      return status;
    }
    free_notify_pool_.Add(*std::move(request));
  }

  for (size_t i = 0; i < kRequestPoolSize; i++) {
    std::optional<usb::Request<>> request;
    status =
        usb::Request<>::Alloc(&request, RNDIS_MAX_XFER_SIZE, BulkOutAddress(), usb_request_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Allocating reads failed %d", status);
      return status;
    }
    free_read_pool_.Add(*std::move(request));
  }

  for (size_t i = 0; i < kRequestPoolSize; i++) {
    std::optional<usb::Request<>> request;
    status =
        usb::Request<>::Alloc(&request, RNDIS_MAX_XFER_SIZE, BulkInAddress(), usb_request_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Allocating writes failed %d", status);
      return status;
    }
    free_write_pool_.Add(*std::move(request));
  }

  status = DdkAdd("rndis-function");
  if (status != ZX_OK) {
    return status;
  }

  function_.SetInterface(this, &usb_function_interface_protocol_ops_);
  return ZX_OK;
}

void RndisFunction::Shutdown() {
  fbl::AutoLock lock(&lock_);
  function_.CancelAll(BulkInAddress());
  function_.CancelAll(BulkOutAddress());
  function_.CancelAll(NotificationAddress());

  // This is necessary for suspend to ensure that the requests are unpinned.
  free_notify_pool_.Release();
  free_read_pool_.Release();
  free_write_pool_.Release();

  ifc_.clear();
}

void RndisFunction::DdkUnbindNew(ddk::UnbindTxn txn) {
  if (cancelled_) {
    txn.Reply();
    return;
  }
  cancelled_ = true;
  cancel_thread_ = std::thread([this, unbind_txn = std::move(txn)]() mutable {
    Shutdown();
    unbind_txn.Reply();
  });
}

void RndisFunction::DdkSuspend(ddk::SuspendTxn txn) {
  cancelled_ = true;
  cancel_thread_ = std::thread([this, suspend_txn = std::move(txn)]() mutable {
    Shutdown();
    suspend_txn.Reply(ZX_OK, 0);
  });
}

void RndisFunction::DdkRelease() {
  cancel_thread_.detach();
  delete this;
}

zx_status_t RndisFunction::Create(void* ctx, zx_device_t* parent) {
  auto device = std::make_unique<RndisFunction>(parent);

  device->Bind();

  // Intentionally leak this device because it's owned by the driver framework.
  __UNUSED auto unused = device.release();
  return ZX_OK;
}

static zx_driver_ops_t rndis_function_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops{};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = RndisFunction::Create;
  return ops;
}();

ZIRCON_DRIVER(rndis_function, rndis_function_driver_ops, "fuchsia", "0.1")
