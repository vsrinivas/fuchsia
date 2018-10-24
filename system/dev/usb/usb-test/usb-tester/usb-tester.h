// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/usb/usb.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/optional.h>
#include <fbl/vector.h>
#include <lib/sync/completion.h>
#include <usb/usb-request.h>
#include <zircon/usb/tester/c/fidl.h>

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
    static fbl::optional<TestRequest> Create(size_t len, uint8_t ep_address);

    ~TestRequest();

    void MoveHelper(TestRequest& other) {
        if (usb_req_) { usb_request_release(usb_req_); }
        usb_req_ = other.usb_req_;
        usb_req_->cookie = &completion_;
        other.usb_req_ = nullptr;
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
    zx_status_t FillData(zircon_usb_tester_DataPatternType data_pattern);

    // Returns the underlying usb request.
    usb_request_t* Get() const { return usb_req_; }

private:
    explicit TestRequest(usb_request_t* usb_req);

    usb_request_t* usb_req_;
    sync_completion_t completion_;
};

class UsbTester;
using UsbTesterBase = ddk::Device<UsbTester, ddk::Messageable, ddk::Unbindable>;

class UsbTester : public UsbTesterBase, public ddk::EmptyProtocol<ZX_PROTOCOL_USB_TESTER> {

public:
    // Spawns device node based on parent node.
    static zx_status_t Create(zx_device_t* parent);

    // Device protocol implementation.
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    void DdkUnbind() { DdkRemove(); }
    void DdkRelease() { delete this; }

    // FIDL message implementation.
    zx_status_t SetModeFwloader();
    // Tests the loopback of data from the bulk OUT EP to the bulk IN EP.
    zx_status_t BulkLoopback(const zircon_usb_tester_TestParams* params);
    zx_status_t IsochLoopback(const zircon_usb_tester_TestParams* params,
                              zircon_usb_tester_IsochResult* result);
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

    UsbTester(zx_device_t* parent, const usb_protocol_t& usb,
              uint8_t bulk_in_addr, uint8_t bulk_out_addr, const IsochLoopbackIntf& isoch_intf)
        : UsbTesterBase(parent),
          usb_(usb),
          bulk_in_addr_(bulk_in_addr),
          bulk_out_addr_(bulk_out_addr),
          isoch_loopback_intf_(isoch_intf) {}

    zx_status_t Bind();

    // Allocates the test requests and adds them to the out_test_reqs list.
    zx_status_t AllocTestReqs(size_t num_reqs, size_t len, uint8_t ep_addr,
                              fbl::Vector<TestRequest>* out_test_reqs);
    // Waits for the completion of each request contained in the test_reqs list in sequential
    // order.
    // The caller should check each request for its completion status.
    void WaitTestReqs(const fbl::Vector<TestRequest>& test_reqs);
    // Fills each request in the test_reqs list with data of the requested data_pattern.
    zx_status_t FillTestReqs(const fbl::Vector<TestRequest>& test_reqs,
                             zircon_usb_tester_DataPatternType data_pattern);
    // Queues all requests contained in the test_reqs list.
    void QueueTestReqs(const fbl::Vector<TestRequest>& test_reqs,
                       uint64_t start_frame);

    // Counts how many requests were successfully loopbacked between the OUT and IN EPs.
    // Returns ZX_OK if no fatal error occurred during verification.
    // out_num_passed will be populated with the number of successfully loopbacked requests.
    zx_status_t VerifyLoopback(const fbl::Vector<TestRequest>& out_reqs,
                               const fbl::Vector<TestRequest>& in_reqs,
                               size_t* out_num_passed);

    usb_protocol_t usb_;

    uint8_t bulk_in_addr_;
    uint8_t bulk_out_addr_;

    IsochLoopbackIntf isoch_loopback_intf_;
};

}  // namespace usb
