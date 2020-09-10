// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_RNDIS_HOST_RNDIS_HOST_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_RNDIS_HOST_RNDIS_HOST_H_

#include <optional>

#include <ddk/protocol/usb.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <fbl/mutex.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "src/connectivity/ethernet/lib/rndis/rndis.h"

class RndisHost;

using RndisHostType = ddk::Device<RndisHost, ddk::Initializable, ddk::Unbindable>;

class RndisHost : public RndisHostType,
                  public ddk::EthernetImplProtocol<RndisHost, ddk::base_protocol> {
 public:
  explicit RndisHost(zx_device_t* parent, uint8_t control_intf, uint8_t bulk_in_addr,
                     uint8_t bulk_out_addr, const usb::UsbDevice& usb);

  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t InitBuffers();
  zx_status_t AddDevice();

  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info);
  void EthernetImplStop();
  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc_);
  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                   size_t data_size);
  void EthernetImplGetBti(zx::bti* out_bti);

 private:
  zx_status_t StartThread();

  void WriteComplete(usb_request_t* request);
  void ReadComplete(usb_request_t* request);

  void Recv(usb_request_t* request);

  // Send a control message to the client device and wait for its response.
  // If successful, the response is stored in control_receive_buffer_.
  zx_status_t Command(void* command);

  // Send a control message to the client device.
  zx_status_t SendControlCommand(void* command);
  // Receive a control message from the client device with the matching request number.
  // If successful, the received message is stored in control_receive_buffer_.
  zx_status_t ReceiveControlMessage(uint32_t request_id);

  zx_status_t InitializeDevice();
  zx_status_t QueryDevice(uint32_t oid, void* info_buffer_out, size_t expected_info_buffer_length);
  zx_status_t SetDeviceOid(uint32_t oid, const void* data, size_t data_length);

  zx_status_t PrepareDataPacket(usb_request_t* req, const void* data, size_t data_length);

  usb::UsbDevice usb_;

  uint8_t mac_addr_[ETH_MAC_SIZE];
  uint8_t control_intf_;
  uint32_t next_request_id_;
  uint32_t mtu_;

  uint8_t bulk_in_addr_;
  uint8_t bulk_out_addr_;

  list_node_t free_read_reqs_;
  list_node_t free_write_reqs_;

  uint64_t rx_endpoint_delay_;  // wait time between 2 recv requests
  uint64_t tx_endpoint_delay_;  // wait time between 2 transmit requests

  // Interface to the ethernet layer.
  ethernet_ifc_protocol_t ifc_;

  std::optional<ddk::InitTxn> init_txn_;

  thrd_t thread_;
  bool thread_started_ = false;
  size_t parent_req_size_;

  fbl::Mutex mutex_;

  uint8_t control_receive_buffer_[RNDIS_CONTROL_BUFFER_SIZE];
};

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_RNDIS_HOST_RNDIS_HOST_H_
