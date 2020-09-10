// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rndis_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/listnode.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define ETH_HEADER_SIZE 4

#define ETHERNET_MAX_TRANSMIT_DELAY 100
#define ETHERNET_MAX_RECV_DELAY 100
#define ETHERNET_TRANSMIT_DELAY 10
#define ETHERNET_RECV_DELAY 10
#define ETHERNET_INITIAL_TRANSMIT_DELAY 0
#define ETHERNET_INITIAL_RECV_DELAY 0

static bool command_succeeded(const void* buf, uint32_t type, size_t length) {
  const auto* header = static_cast<const rndis_header_complete*>(buf);
  if (header->msg_type != type) {
    zxlogf(TRACE, "Bad type: Actual: %x, Expected: %x.", header->msg_type, type);
    return false;
  }
  if (header->msg_length != length) {
    zxlogf(TRACE, "Bad length: Actual: %u, Expected: %zu.", header->msg_length, length);
    return false;
  }
  if (header->status != RNDIS_STATUS_SUCCESS) {
    zxlogf(TRACE, "Bad status: %x.", header->status);
    return false;
  }
  return true;
}

template <typename T>
bool command_succeeded(const T* buf, uint32_t type) {
  return command_succeeded(buf, type, sizeof(*buf));
}

zx_status_t RndisHost::SendControlCommand(void* command) {
  rndis_header* header = static_cast<rndis_header*>(command);
  header->request_id = next_request_id_++;

  return usb_.ControlOut(USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                         USB_CDC_SEND_ENCAPSULATED_COMMAND, 0, control_intf_, RNDIS_CONTROL_TIMEOUT,
                         command, header->msg_length);
}

zx_status_t RndisHost::ReceiveControlMessage(uint32_t request_id) {
  size_t len_read = 0;
  zx_status_t status =
      usb_.ControlIn(USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                     USB_CDC_GET_ENCAPSULATED_RESPONSE, 0, control_intf_, RNDIS_CONTROL_TIMEOUT,
                     control_receive_buffer_, sizeof(control_receive_buffer_), &len_read);
  if (len_read == 0) {
    zxlogf(ERROR, "rndishost received a zero-length response on the control channel");
    return ZX_ERR_IO_REFUSED;
  }
  const auto* header = reinterpret_cast<rndis_header*>(control_receive_buffer_);
  if (header->request_id != request_id) {
    zxlogf(ERROR, "rndishost received wrong packet ID on control channel: got %d, wanted %d",
           header->request_id, request_id);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return status;
}

zx_status_t RndisHost::Command(void* command) {
  zx_status_t status = SendControlCommand(command);
  if (status != ZX_OK) {
    return status;
  }
  const uint32_t request_id = static_cast<rndis_header*>(command)->request_id;
  return ReceiveControlMessage(request_id);
}

void RndisHost::Recv(usb_request_t* request) {
  void* read_data;
  zx_status_t status = usb_request_mmap(request, &read_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "rndishost receive: usb_request_mmap failed: %d", status);
    return;
  }

  size_t remaining = request->response.actual;
  while (remaining >= sizeof(rndis_packet_header)) {
    const auto* header = static_cast<const rndis_packet_header*>(read_data);

    if (header->msg_type != RNDIS_PACKET_MSG) {
      zxlogf(ERROR, "rndishost receive: bad data packet type %u", header->msg_type);
      return;
    }
    if (header->msg_length > remaining) {
      zxlogf(ERROR,
             "rndishost receive: bad data packet message length: %u bytes, but only %zu bytes "
             "remain\n",
             header->msg_length, remaining);
      return;
    }
    if (header->msg_length < sizeof(*header)) {
      zxlogf(ERROR,
             "rndishost receive: bad data packet message length: %u bytes is smaller than header\n",
             header->msg_length);
      return;
    }
    if (header->data_length > header->msg_length) {
      zxlogf(ERROR,
             "rndishost receive: bad data packet data length: %u bytes is longer than message of "
             "%u bytes\n",
             header->data_length, header->msg_length);
      return;
    }
    static_assert(sizeof(*header) >= offsetof(rndis_packet_header, data_offset));
    if (header->data_offset > header->msg_length - offsetof(rndis_packet_header, data_offset)) {
      zxlogf(ERROR,
             "rndishost receive: bad data packet data offset: %zu + %u bytes is after message of "
             "%u bytes\n",
             offsetof(rndis_packet_header, data_offset), header->data_offset, header->msg_length);
      return;
    }

    size_t total_offset = offsetof(rndis_packet_header, data_offset) + header->data_offset;
    if (total_offset > header->msg_length - header->data_length) {
      zxlogf(ERROR,
             "rndishost receive: bad data packet: data continues after end of message (message "
             "length = %u; data offset = %zu, data length = %u)\n",
             header->msg_length, total_offset, header->data_length);
      return;
    }

    ethernet_ifc_recv(&ifc_, static_cast<uint8_t*>(read_data) + total_offset, header->data_length,
                      0);

    read_data = static_cast<uint8_t*>(read_data) + header->msg_length;
    remaining -= header->msg_length;
  }
}

void RndisHost::ReadComplete(usb_request_t* request) {
  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  fbl::AutoLock lock(&mutex_);
  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(DEBUG, "rndis_read_complete usb_reset_endpoint");
    usb_.ResetEndpoint(bulk_in_addr_);
  } else if (request->response.status == ZX_ERR_IO_INVALID) {
    zxlogf(DEBUG,
           "rndis_read_complete Slowing down the requests by %d usec"
           " and resetting the recv endpoint\n",
           ETHERNET_RECV_DELAY);
    if (rx_endpoint_delay_ < ETHERNET_MAX_RECV_DELAY) {
      rx_endpoint_delay_ += ETHERNET_RECV_DELAY;
    }
    usb_.ResetEndpoint(bulk_in_addr_);
  }
  if (request->response.status == ZX_OK && ifc_.ops) {
    Recv(request);
  } else {
    zxlogf(TRACE, "rndis read complete: bad status = %d", request->response.status);
  }

  // TODO: Only usb_request_queue if the device is online.
  zx_nanosleep(zx_deadline_after(ZX_USEC(rx_endpoint_delay_)));
  usb_request_complete_t complete = {
      .callback = [](void* arg, usb_request_t* request) -> void {
        static_cast<RndisHost*>(arg)->ReadComplete(request);
      },
      .ctx = this,
  };
  usb_.RequestQueue(request, &complete);
}

void RndisHost::WriteComplete(usb_request_t* request) {
  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    zxlogf(ERROR, "rndis_write_complete zx_err_io_not_present");
    usb_request_release(request);
    return;
  }

  fbl::AutoLock lock(&mutex_);
  if (request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(DEBUG, "rndishost usb_reset_endpoint");
    usb_.ResetEndpoint(bulk_out_addr_);
  } else if (request->response.status == ZX_ERR_IO_INVALID) {
    zxlogf(DEBUG,
           "rndis_write_complete Slowing down the requests by %d usec"
           " and resetting the transmit endpoint\n",
           ETHERNET_TRANSMIT_DELAY);
    if (tx_endpoint_delay_ < ETHERNET_MAX_TRANSMIT_DELAY) {
      tx_endpoint_delay_ += ETHERNET_TRANSMIT_DELAY;
    }
    usb_.ResetEndpoint(bulk_out_addr_);
  }

  zx_status_t status = usb_req_list_add_tail(&free_write_reqs_, request, parent_req_size_);
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

RndisHost::RndisHost(zx_device_t* parent, uint8_t control_intf, uint8_t bulk_in_addr,
                     uint8_t bulk_out_addr, const usb::UsbDevice& usb)
    : RndisHostType(parent),
      usb_(usb),
      mac_addr_{},
      control_intf_(control_intf),
      next_request_id_(0),
      mtu_(0),
      bulk_in_addr_(bulk_in_addr),
      bulk_out_addr_(bulk_out_addr),
      rx_endpoint_delay_(0),
      tx_endpoint_delay_(0),
      ifc_({}),
      thread_started_(false),
      parent_req_size_(usb.GetRequestSize()) {
  list_initialize(&free_read_reqs_);
  list_initialize(&free_write_reqs_);

  ifc_.ops = nullptr;
}

zx_status_t RndisHost::EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
  if (options) {
    return ZX_ERR_INVALID_ARGS;
  }

  memset(info, 0, sizeof(*info));
  info->mtu = mtu_;
  memcpy(info->mac, mac_addr_, sizeof(mac_addr_));
  info->netbuf_size = sizeof(ethernet_netbuf_t);

  return ZX_OK;
}

void RndisHost::EthernetImplStop() {
  fbl::AutoLock lock(&mutex_);
  ifc_.ops = nullptr;
}

zx_status_t RndisHost::EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&mutex_);
  if (ifc_.ops) {
    return ZX_ERR_ALREADY_BOUND;
  }

  ifc_ = *ifc;
  // TODO: Check that the device is online before sending ETHERNET_STATUS_ONLINE.
  ethernet_ifc_status(&ifc_, ETHERNET_STATUS_ONLINE);
  return ZX_OK;
}

void RndisHost::EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                    ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  zx_status_t status = ZX_OK;

  fbl::AutoLock lock(&mutex_);

  usb_request_t* req = usb_req_list_remove_head(&free_write_reqs_, parent_req_size_);
  if (req == nullptr) {
    zxlogf(DEBUG, "rndishost dropped a packet");
    status = ZX_ERR_NO_RESOURCES;
    goto done;
  }

  status = PrepareDataPacket(req, netbuf->data_buffer, netbuf->data_size);
  if (status != ZX_OK) {
    status = usb_req_list_add_tail(&free_write_reqs_, req, parent_req_size_);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    goto done;
  }

  zx_nanosleep(zx_deadline_after(ZX_USEC(tx_endpoint_delay_)));
  {
    usb_request_complete_t complete = {
        .callback = [](void* arg, usb_request_t* request) -> void {
          static_cast<RndisHost*>(arg)->WriteComplete(request);
        },
        .ctx = this,
    };
    usb_.RequestQueue(req, &complete);
  }

done:
  lock.release();
  completion_cb(cookie, status, netbuf);
}

zx_status_t RndisHost::PrepareDataPacket(usb_request_t* req, const void* data, size_t data_length) {
  if (data_length > RNDIS_MAX_DATA_SIZE) {
    zxlogf(ERROR, "rndishost: data packet too large (%zu bytes, maximum %u)", data_length,
           RNDIS_MAX_DATA_SIZE);
    return ZX_ERR_IO_OVERRUN;
  }
  static_assert(sizeof(rndis_packet_header) < UINT32_MAX &&
                RNDIS_MAX_DATA_SIZE < UINT32_MAX - sizeof(rndis_packet_header));

  rndis_packet_header header{};
  header.msg_type = RNDIS_PACKET_MSG;
  header.msg_length = static_cast<uint32_t>(sizeof(header) + data_length);
  header.data_offset = sizeof(header) - offsetof(rndis_packet_header, data_offset);
  header.data_length = static_cast<uint32_t>(data_length);

  ssize_t bytes_copied = usb_request_copy_to(req, &header, sizeof(header), 0);
  if (bytes_copied < 0) {
    zxlogf(ERROR, "rndishost: failed to copy request header into send txn (error %zd)",
           bytes_copied);
    return ZX_ERR_IO;
  }
  if (static_cast<size_t>(bytes_copied) < sizeof(header)) {
    zxlogf(ERROR,
           "rndishost: failed to copy whole request header into send txn (copied %zd out of %zu "
           "bytes)\n",
           bytes_copied, sizeof(header));
    return ZX_ERR_IO_OVERRUN;
  }

  bytes_copied = usb_request_copy_to(req, data, data_length, sizeof(header));
  if (bytes_copied < 0) {
    zxlogf(ERROR, "rndishost: failed to copy data into send txn (error %zd)", bytes_copied);
    return ZX_ERR_IO;
  }
  if (static_cast<size_t>(bytes_copied) < data_length) {
    zxlogf(ERROR,
           "rndishost: failed to copy all data into send txn (copied %zd out of %zu bytes)\n",
           bytes_copied, data_length);
    return ZX_ERR_IO_OVERRUN;
  }

  req->header.length = sizeof(header) + data_length;
  return ZX_OK;
}

void RndisHost::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void RndisHost::DdkRelease() {
  if (thread_started_) {
    thrd_join(thread_, NULL);
  }

  usb_request_t* txn;
  while ((txn = usb_req_list_remove_head(&free_read_reqs_, parent_req_size_)) != NULL) {
    usb_request_release(txn);
  }
  while ((txn = usb_req_list_remove_head(&free_write_reqs_, parent_req_size_)) != NULL) {
    usb_request_release(txn);
  }
}

zx_status_t RndisHost::EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                            size_t data_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

void RndisHost::EthernetImplGetBti(zx::bti* out_bti) {}

// Send an initialization message to the device.
zx_status_t RndisHost::InitializeDevice() {
  rndis_init init{};
  init.msg_type = RNDIS_INITIALIZE_MSG;
  init.msg_length = sizeof(init);
  init.major_version = RNDIS_MAJOR_VERSION;
  init.minor_version = RNDIS_MINOR_VERSION;
  init.max_xfer_size = RNDIS_MAX_XFER_SIZE;

  zx_status_t status = Command(&init);
  if (status != ZX_OK) {
    zxlogf(ERROR, "rndishost bad status on initial message. %d", status);
    return status;
  }

  rndis_init_complete* init_cmplt = reinterpret_cast<rndis_init_complete*>(control_receive_buffer_);
  if (!command_succeeded(init_cmplt, RNDIS_INITIALIZE_CMPLT)) {
    zxlogf(ERROR, "rndishost initialization failed.");
    return ZX_ERR_IO;
  }

  zxlogf(INFO, "rndishost maximum bus transfer size: %u bytes", init_cmplt->max_xfer_size);
  return ZX_OK;
}

zx_status_t RndisHost::QueryDevice(uint32_t oid, void* info_buffer_out,
                                   size_t expected_info_buffer_length) {
  rndis_query query{};
  query.msg_type = RNDIS_QUERY_MSG;
  query.msg_length = sizeof(query);
  query.oid = oid;
  query.info_buffer_length = 0;
  query.info_buffer_offset = 0;

  zx_status_t status = SendControlCommand(&query);
  if (status != ZX_OK) {
    zxlogf(ERROR, "rndishost failed to issue query: %d", status);
    return status;
  }

  status = ReceiveControlMessage(query.request_id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "rndishost failed to receive query response: %d", status);
    return status;
  }

  rndis_query_complete* query_cmplt =
      reinterpret_cast<rndis_query_complete*>(control_receive_buffer_);
  if (!command_succeeded(control_receive_buffer_, RNDIS_QUERY_CMPLT,
                         sizeof(*query_cmplt) + expected_info_buffer_length)) {
    return ZX_ERR_IO;
  }

  // info_buffer_offset and info_buffer_length determine where the query result is in the response
  // buffer. Check that the length of the result matches what we expect.
  if (query_cmplt->info_buffer_length != expected_info_buffer_length) {
    zxlogf(ERROR, "rndishost expected info buffer of size %zu, got %u", expected_info_buffer_length,
           query_cmplt->info_buffer_length);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (query_cmplt->info_buffer_offset == 0 || query_cmplt->info_buffer_length == 0) {
    // Section 2.2.10 (REMOTE_NDIS_QUERY_CMPLT), p. 20 of the RNDIS specification states that if
    // there is no payload, both the offset and length must be set to 0. It does not expressly
    // forbid a nonempty payload with a zero offset, but we assume it is meant to be forbidden.
    if (query_cmplt->info_buffer_offset != 0 || query_cmplt->info_buffer_length != 0) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    // Both the offset and the length are zero. As the length equals expected_info_buffer_length, we
    // were expecting an empty response to this query. (It is unclear when this might happen, but it
    // is permitted.)
    return ZX_OK;
  }

  // The offset in info_buffer_offset is given in bytes from from the beginning of request_id. Check
  // that it doesn't begin outside the response buffer. This also ensures that computing the total
  // offset from the start of the buffer does not overflow.
  if (query_cmplt->info_buffer_offset >=
      sizeof(control_receive_buffer_) - offsetof(rndis_query_complete, request_id)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Check that the length + offset lies within the buffer. From the previous check, we know that
  // total_offset < sizeof(control_receive_buffer_), and therefore the subtraction won't underflow.
  const ptrdiff_t total_offset =
      offsetof(rndis_query, request_id) + query_cmplt->info_buffer_offset;
  if (query_cmplt->info_buffer_length > sizeof(control_receive_buffer_) - total_offset) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (info_buffer_out != nullptr) {
    memcpy(info_buffer_out, control_receive_buffer_ + total_offset, expected_info_buffer_length);
  }

  return ZX_OK;
}

zx_status_t RndisHost::SetDeviceOid(uint32_t oid, const void* data, size_t data_length) {
  struct Payload {
    rndis_set header;
    uint8_t data[RNDIS_SET_INFO_BUFFER_LENGTH];
  } __PACKED;

  Payload set = {};
  set.header.msg_type = RNDIS_SET_MSG;
  set.header.msg_length = sizeof(rndis_set);
  set.header.info_buffer_length = 0;
  set.header.info_buffer_offset = 0;
  set.header.oid = oid;

  if (data_length > 0) {
    if (data_length > RNDIS_SET_INFO_BUFFER_LENGTH) {
      zxlogf(ERROR, "rndishost attempted to set OID %u with size %zu bytes (maximum is %d)", oid,
             data_length, RNDIS_SET_INFO_BUFFER_LENGTH);
      return ZX_ERR_INVALID_ARGS;
    }

    // The buffer is always the same size regardless of the size of the payload.
    set.header.msg_length += RNDIS_SET_INFO_BUFFER_LENGTH;
    set.header.info_buffer_length = RNDIS_SET_INFO_BUFFER_LENGTH;
    set.header.info_buffer_offset = offsetof(Payload, data) - offsetof(rndis_set, request_id);
    memcpy(&set.data, data, data_length);
  }

  zx_status_t status = Command(&set);
  if (status != ZX_OK) {
    zxlogf(ERROR, "rndishost issuing set command failed: %d", status);
    return status;
  }

  rndis_set_complete* set_cmplt = reinterpret_cast<rndis_set_complete*>(control_receive_buffer_);
  if (!command_succeeded(set_cmplt, RNDIS_SET_CMPLT)) {
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t RndisHost::StartThread() {
  ZX_DEBUG_ASSERT(init_txn_.has_value());

  zx_status_t status = InitializeDevice();
  if (status != ZX_OK) {
    goto fail;
  }

  status = QueryDevice(OID_802_3_PERMANENT_ADDRESS, mac_addr_, sizeof(mac_addr_));
  if (status != ZX_OK) {
    zxlogf(ERROR, "rndishost could not obtain device physical address: %d", status);
    goto fail;
  }
  zxlogf(INFO, "rndishost MAC address: %02x:%02x:%02x:%02x:%02x:%02x", mac_addr_[0], mac_addr_[1],
         mac_addr_[2], mac_addr_[3], mac_addr_[4], mac_addr_[5]);

  status = QueryDevice(OID_GEN_MAXIMUM_FRAME_SIZE, &mtu_, sizeof(mtu_));
  if (status != ZX_OK) {
    zxlogf(ERROR, "rndishost could not obtain maximum frame size: %d", status);
    goto fail;
  }
  zxlogf(INFO, "rndishost maximum frame size: %u bytes", mtu_);

  {
    // The device's packet filter is initialized to 0, which blocks all traffic. Enable network
    // traffic.
    const uint32_t filter = RNDIS_PACKET_TYPE_DIRECTED | RNDIS_PACKET_TYPE_BROADCAST |
                            RNDIS_PACKET_TYPE_ALL_MULTICAST | RNDIS_PACKET_TYPE_PROMISCUOUS;
    status = SetDeviceOid(OID_GEN_CURRENT_PACKET_FILTER, &filter, sizeof(filter));
    if (status != ZX_OK) {
      zxlogf(ERROR, "rndishost failed to set packet filter");
      goto fail;
    }
  }

  // Queue read requests
  {
    fbl::AutoLock lock(&mutex_);
    usb_request_t* txn;
    usb_request_complete_t complete = {
        .callback = [](void* arg, usb_request_t* request) -> void {
          static_cast<RndisHost*>(arg)->ReadComplete(request);
        },
        .ctx = this,
    };
    while ((txn = usb_req_list_remove_head(&free_read_reqs_, parent_req_size_)) != nullptr) {
      usb_.RequestQueue(txn, &complete);
    }
  }

  init_txn_->Reply(ZX_OK);  // This will make the device visible and able to be unbound.
  zxlogf(INFO, "rndishost ready");
  return ZX_OK;

fail:
  init_txn_->Reply(status);  // This will schedule unbinding of the device.
  return status;
}

zx_status_t RndisHost::AddDevice() {
  zx_status_t status = ZX_OK;

  uint64_t req_size = parent_req_size_ + sizeof(usb_req_internal_t);

  for (int i = 0; i < READ_REQ_COUNT; i++) {
    usb_request_t* req;
    status = usb_request_alloc(&req, RNDIS_MAX_XFER_SIZE, bulk_in_addr_, req_size);
    if (status != ZX_OK) {
      return status;
    }
    status = usb_req_list_add_head(&free_read_reqs_, req, parent_req_size_);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
  for (int i = 0; i < WRITE_REQ_COUNT; i++) {
    usb_request_t* req;
    // TODO: Allocate based on mtu.
    status = usb_request_alloc(&req, RNDIS_BUFFER_SIZE, bulk_out_addr_, req_size);
    if (status != ZX_OK) {
      return status;
    }
    status = usb_req_list_add_head(&free_write_reqs_, req, parent_req_size_);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  status = DdkAdd(ddk::DeviceAddArgs("rndishost").set_proto_id(ZX_PROTOCOL_ETHERNET_IMPL));
  if (status != ZX_OK) {
    zxlogf(ERROR, "rndishost: failed to create device: %d", status);
    return status;
  }
  return ZX_OK;
}

void RndisHost::DdkInit(ddk::InitTxn txn) {
  init_txn_ = std::move(txn);
  thread_started_ = true;
  int ret = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return static_cast<RndisHost*>(arg)->StartThread(); }, this,
      "rndishost_start_thread");
  if (ret != thrd_success) {
    thread_started_ = false;
    return init_txn_->Reply(ZX_ERR_NO_RESOURCES);
  }
  // The thread will reply to |init_txn_| once it is ready to make the device visible
  // and able to be unbound.
}

static zx_status_t rndishost_bind(void* ctx, zx_device_t* parent) {
  usb::UsbDevice usb;
  zx_status_t status = usb::UsbDevice::CreateFromDevice(parent, &usb);
  if (status != ZX_OK) {
    return status;
  }

  uint8_t bulk_in_addr = 0;
  uint8_t bulk_out_addr = 0;
  uint8_t intr_addr = 0;
  uint8_t control_intf = 0;
  {
    // Find our endpoints.
    // We should have two interfaces: the CDC classified interface the bulk in
    // and out endpoints, and the RNDIS interface for control. The RNDIS
    // interface will be classified as USB_CLASS_WIRELESS when the device is
    // used for tethering.
    // TODO: Figure out how to handle other RNDIS use cases.
    std::optional<usb::InterfaceList> interfaces;
    status = usb::InterfaceList::Create(usb, false, &interfaces);
    if (status != ZX_OK) {
      return status;
    }
    for (const usb::Interface& interface : *interfaces) {
      const usb_interface_descriptor_t* intf = interface.descriptor();
      if (intf->bInterfaceClass == USB_CLASS_WIRELESS) {
        control_intf = intf->bInterfaceNumber;
        if (intf->bNumEndpoints != 1) {
          return ZX_ERR_NOT_SUPPORTED;
        }
        for (const auto& endp : interface.GetEndpointList()) {
          if (usb_ep_direction(&endp.descriptor) == USB_ENDPOINT_IN &&
              usb_ep_type(&endp.descriptor) == USB_ENDPOINT_INTERRUPT) {
            intr_addr = endp.descriptor.bEndpointAddress;
          }
        }
      } else if (intf->bInterfaceClass == USB_CLASS_CDC) {
        if (intf->bNumEndpoints != 2) {
          return ZX_ERR_NOT_SUPPORTED;
        }
        for (const auto& endp : interface.GetEndpointList()) {
          if (usb_ep_direction(&endp.descriptor) == USB_ENDPOINT_OUT) {
            if (usb_ep_type(&endp.descriptor) == USB_ENDPOINT_BULK) {
              bulk_out_addr = endp.descriptor.bEndpointAddress;
            }
          } else if (usb_ep_direction(&endp.descriptor) == USB_ENDPOINT_IN) {
            if (usb_ep_type(&endp.descriptor) == USB_ENDPOINT_BULK) {
              bulk_in_addr = endp.descriptor.bEndpointAddress;
            }
          }
        }
      } else {
        return ZX_ERR_NOT_SUPPORTED;
      }
    }
  }

  if (!bulk_in_addr || !bulk_out_addr || !intr_addr) {
    zxlogf(ERROR, "rndishost couldn't find endpoints");
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<RndisHost>(&ac, parent, control_intf, bulk_in_addr,
                                                 bulk_out_addr, usb);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = dev->AddDevice();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev, so we don't own it any more.
    dev.release();
  } else {
    zxlogf(ERROR, "rndishost_bind failed: %d", status);
  }
  return status;
}

static zx_driver_ops_t rndis_driver_ops = []() {
  zx_driver_ops_t ops{};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = rndishost_bind;
  return ops;
}();

// TODO: Make sure we can bind to all RNDIS use cases. USB_CLASS_WIRELESS only
// covers the tethered device case.
// clang-format off
ZIRCON_DRIVER_BEGIN(rndishost, rndis_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_WIRELESS),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_SUBCLASS_WIRELESS_MISC),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, USB_PROTOCOL_WIRELESS_MISC_RNDIS),
ZIRCON_DRIVER_END(rndishost)
