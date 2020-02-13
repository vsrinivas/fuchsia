// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_TEST_USB_TESTER_USB_TESTER_H_
#define SRC_DEVICES_USB_DRIVERS_USB_TEST_USB_TESTER_USB_TESTER_H_

#include <fuchsia/hardware/usb/tester/c/fidl.h>
#include <lib/sync/completion.h>

#include <optional>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/array.h>
#include <fbl/vector.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

namespace usb {

// Wrapper around usb_request that provides convenience methods for
// filling the usb_request with data and waiting for the request completion.
//
// Example:
//    auto req = TestRequest::Create(len, ep_addr);
//    ...
//    zx_status_t status = req->FillData(params->data_pattern);
//    ...
//    usb_request_queue(&usb_, req->Get());
//    zx_status_t wait_status = req->WaitComplete(&usb_);
class TestRequest {
 public:
  // Creates a request for transferring |len| bytes at the given |ep_address|.
  static std::optional<TestRequest> Create(size_t len, uint8_t ep_address, size_t parent_req_size,
                                           bool set_cb = true, bool expect_cb = true);

  // Creates a request for transferring data using the given scatter gather list.
  static std::optional<TestRequest> Create(const fuchsia_hardware_usb_tester_SgList& sg_list,
                                           uint8_t ep_address, size_t parent_req_size,
                                           bool set_cb = true, bool expect_cb = true);
  ~TestRequest();

  void MoveHelper(TestRequest& other) {
    if (usb_req_) {
      usb_request_release(usb_req_);
    }
    usb_req_ = other.usb_req_;
    other.usb_req_ = nullptr;
    expect_cb_ = other.expect_cb_;
    got_cb_ = other.got_cb_;
  }

  TestRequest(TestRequest&& other) : usb_req_(nullptr) { MoveHelper(other); }

  TestRequest& operator=(TestRequest&& other) {
    MoveHelper(other);
    return *this;
  }

  // Waits for the request to complete and verifies its completion status and transferred length.
  // Returns ZX_OK if the request completed successfully, and the transferred length equals the
  // requested length.
  zx_status_t WaitComplete(usb_protocol_t* usb);
  // Fills the given test request with data of the requested pattern.
  zx_status_t FillData(fuchsia_hardware_usb_tester_DataPatternType data_pattern);
  // Copies the request data into a newly created array where the data will be contiguous,
  // and populates |out_scattered| with the array address.
  zx_status_t GetDataUnscattered(fbl::Array<uint8_t>* out_unscattered);

  // Returns the underlying usb request.
  usb_request_t* Get() const { return usb_req_; }
  usb_request_complete_t* GetCompleteCb() { return &req_complete_; }

  bool expect_cb() const { return expect_cb_; }
  bool got_cb() const { return got_cb_; }

 private:
  explicit TestRequest(usb_request_t* usb_req, bool set_cb, bool expect_cb);
  static void RequestCompleteCallback(void* ctx, usb_request_t* request);
  usb_request_complete_t req_complete_ = {
      .callback = RequestCompleteCallback,
      .ctx = this,
  };
  usb_request_t* usb_req_;
  sync_completion_t completion_;

  bool expect_cb_;
  bool got_cb_;
};

class UsbTester;
using UsbTesterBase = ddk::Device<UsbTester, ddk::Messageable, ddk::UnbindableNew>;

class UsbTester : public UsbTesterBase, public ddk::EmptyProtocol<ZX_PROTOCOL_USB_TESTER> {
 public:
  // Spawns device node based on parent node.
  static zx_status_t Create(zx_device_t* parent);

  // Device protocol implementation.
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  // FIDL message implementation.
  zx_status_t SetModeFwloader();
  // Tests the loopback of data from the bulk OUT EP to the bulk IN EP.
  zx_status_t BulkLoopback(const fuchsia_hardware_usb_tester_BulkTestParams* params,
                           const fuchsia_hardware_usb_tester_SgList* out_sg_list,
                           const fuchsia_hardware_usb_tester_SgList* in_sg_list);
  zx_status_t IsochLoopback(const fuchsia_hardware_usb_tester_IsochTestParams* params,
                            fuchsia_hardware_usb_tester_IsochResult* result);
  void GetVersion(uint8_t* major_version, uint8_t* minor_version);

 private:
  struct IsochLoopbackIntf {
    uint8_t intf_num;
    uint8_t alt_setting;

    uint8_t in_addr;
    uint8_t out_addr;
    uint16_t in_max_packet;
    uint16_t out_max_packet;
  };

  UsbTester(zx_device_t* parent, const usb_protocol_t& usb, uint8_t bulk_in_addr,
            uint8_t bulk_out_addr, const IsochLoopbackIntf& isoch_intf, size_t parent_req_size)
      : UsbTesterBase(parent),
        usb_(usb),
        bulk_in_addr_(bulk_in_addr),
        bulk_out_addr_(bulk_out_addr),
        isoch_loopback_intf_(isoch_intf),
        parent_req_size_(parent_req_size) {}

  zx_status_t Bind();

  // Allocates the test requests and adds them to the out_test_reqs list.
  zx_status_t AllocIsochTestReqs(size_t num_reqs, size_t len, uint8_t ep_addr,
                                 fbl::Vector<TestRequest>* out_test_reqs, size_t parent_req_size,
                                 const fuchsia_hardware_usb_tester_PacketOptions* packet_opts,
                                 size_t packet_opts_len);
  // Waits for the completion of each request contained in the test_reqs list in sequential
  // order.
  // The caller should check each request for its completion status.
  void WaitTestReqs(const fbl::Vector<TestRequest>& test_reqs);
  // Fills each request in the test_reqs list with data of the requested data_pattern.
  zx_status_t FillTestReqs(const fbl::Vector<TestRequest>& test_reqs,
                           fuchsia_hardware_usb_tester_DataPatternType data_pattern);
  // Queues all requests contained in the test_reqs list.
  void QueueTestReqs(const fbl::Vector<TestRequest>& test_reqs, uint64_t start_frame);

  // Counts how many requests were successfully loopbacked between the OUT and IN EPs.
  // Returns ZX_OK if no fatal error occurred during verification.
  // out_num_passed will be populated with the number of successfully loopbacked requests.
  zx_status_t VerifyLoopback(const fbl::Vector<TestRequest>& out_reqs,
                             const fbl::Vector<TestRequest>& in_reqs, size_t* out_num_passed);
  // Returns ZX_OK if callbacks were received only when expected.
  zx_status_t VerifyCallbacks(const fbl::Vector<TestRequest>& reqs);

  usb_protocol_t usb_;

  uint8_t bulk_in_addr_;
  uint8_t bulk_out_addr_;

  IsochLoopbackIntf isoch_loopback_intf_;
  size_t parent_req_size_;
};

}  // namespace usb

#endif  // SRC_DEVICES_USB_DRIVERS_USB_TEST_USB_TESTER_USB_TESTER_H_
