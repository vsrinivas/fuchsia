// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-tester.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/usb-composite.h>
#include <ddk/usb/usb.h>
#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>
#include <usb/usb-request.h>
#include <lib/sync/completion.h>
#include <zircon/hw/usb.h>
#include <zircon/usb/tester/c/fidl.h>

#include <stdlib.h>
#include <string.h>

#include "usb-tester-hw.h"

namespace {

constexpr uint64_t kReqMaxLen = 0x10000;  // 64 K
constexpr uint32_t kReqTimeoutSecs = 5;
constexpr uint8_t kTestDummyData = 42;

constexpr uint64_t kIsochStartFrameDelay = 5;
constexpr uint64_t kIsochAdditionalInReqs = 8;

inline uint8_t MSB(int n) { return static_cast<uint8_t>(n >> 8); }
inline uint8_t LSB(int n) { return static_cast<uint8_t>(n & 0xFF); }

}  // namespace

namespace usb {

fbl::optional<TestRequest> TestRequest::Create(size_t len, uint8_t ep_address) {
    usb_request_t* usb_req;
    zx_status_t status = usb_request_alloc(&usb_req, len, ep_address, sizeof(usb_request_t));
    if (status != ZX_OK) {
        return fbl::optional<TestRequest>();
    }
    return fbl::optional<TestRequest>(TestRequest(usb_req));
}

TestRequest::TestRequest(usb_request_t* usb_req) : usb_req_(usb_req) {
    usb_req->cookie = &completion_;
    usb_req->complete_cb = [](usb_request_t* req, void* cookie) -> void {
        ZX_DEBUG_ASSERT(cookie != nullptr);
        sync_completion_signal(reinterpret_cast<sync_completion_t*>(cookie));
    };
}

TestRequest::~TestRequest() {
    if (usb_req_) {
        usb_request_release(usb_req_);
    }
}

zx_status_t TestRequest::WaitComplete(usb_protocol_t* usb) {
    usb_request_t* req = Get();
    zx_status_t status = sync_completion_wait(&completion_, ZX_SEC(kReqTimeoutSecs));
    if (status == ZX_OK) {
        status = req->response.status;
        if (status == ZX_OK) {
            if (req->response.actual != req->header.length) {
                status = ZX_ERR_IO;
            }
        } else if (status == ZX_ERR_IO_REFUSED) {
             usb_reset_endpoint(usb, req->header.ep_address);
        }
        return status;
    } else if (status == ZX_ERR_TIMED_OUT) {
        // Cancel the request before returning.
        status = usb_cancel_all(usb, req->header.ep_address);
        if (status != ZX_OK) {
            zxlogf(ERROR, "failed to cancel usb transfers, err: %d\n", status);
            return ZX_ERR_TIMED_OUT;
        }
        status = sync_completion_wait(&completion_, ZX_TIME_INFINITE);
        if (status != ZX_OK) {
            zxlogf(ERROR, "failed to wait for request completion after cancelling request\n");
        }
        return ZX_ERR_TIMED_OUT;
    } else {
        return status;
    }
}

zx_status_t TestRequest::FillData(zircon_usb_tester_DataPatternType data_pattern) {
    uint8_t* buf;
    zx_status_t status = usb_request_mmap(Get(), (void**)&buf);
    if (status != ZX_OK) {
        return status;
    }
    for (size_t i = 0; i < Get()->header.length; ++i) {
        switch (data_pattern) {
        case zircon_usb_tester_DataPatternType_CONSTANT:
            buf[i] = kTestDummyData;
            break;
        case zircon_usb_tester_DataPatternType_RANDOM:
            buf[i] = static_cast<uint8_t>(rand() % 256);
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }
    }
    return ZX_OK;
}

zx_status_t UsbTester::AllocTestReqs(size_t num_reqs, size_t len, uint8_t ep_addr,
                                     fbl::Vector<TestRequest>* out_test_reqs) {

    fbl::AllocChecker ac;
    out_test_reqs->reserve(num_reqs, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    for (size_t i = 0; i < num_reqs; ++i) {
        auto test_req = TestRequest::Create(len, ep_addr);
        if (!test_req.has_value()) {
            return ZX_ERR_NO_MEMORY;
        }
        out_test_reqs->push_back(fbl::move(test_req.value()));
    }
    return ZX_OK;
}

void UsbTester::WaitTestReqs(const fbl::Vector<TestRequest>& test_reqs) {
    for (auto& test_req : test_reqs) {
        test_req.WaitComplete(&usb_);
    }
}

zx_status_t UsbTester::FillTestReqs(const fbl::Vector<TestRequest>& test_reqs,
                                    zircon_usb_tester_DataPatternType data_pattern) {
    for (auto& test_req : test_reqs) {
        zx_status_t status = test_req.FillData(data_pattern);
        if (status != ZX_OK) {
            return status;
        }
    }
    return ZX_OK;
}

void UsbTester::QueueTestReqs(const fbl::Vector<TestRequest>& test_reqs,
                              uint64_t start_frame) {
    bool first = true;
    for (auto& test_req : test_reqs) {
        usb_request_t* usb_req = test_req.Get();
        // Set the frame ID for the first request.
        // The following requests will be scheduled for ASAP after that.
        if (first) {
            usb_req->header.frame = start_frame;
            first = false;
        }
        usb_request_queue(&usb_, usb_req);
    }
}

zx_status_t UsbTester::SetModeFwloader() {
    size_t out_len;
    zx_status_t status = usb_control(&usb_,
                                     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                     USB_TESTER_SET_MODE_FWLOADER, 0, 0, nullptr, 0,
                                     ZX_SEC(kReqTimeoutSecs), &out_len);
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to set mode fwloader, err: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx_status_t UsbTester::BulkLoopback(const zircon_usb_tester_TestParams* params) {
    if (params->len > kReqMaxLen) {
        return ZX_ERR_INVALID_ARGS;
    }
    auto out_req = TestRequest::Create(params->len, bulk_out_addr_);
    if (!out_req.has_value()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto in_req = TestRequest::Create(params->len, bulk_in_addr_);
    if (!in_req.has_value()) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = out_req->FillData(params->data_pattern);
    if (status != ZX_OK) {
        return status;
    }
    usb_request_queue(&usb_, out_req->Get());
    usb_request_queue(&usb_, in_req->Get());

    zx_status_t out_status = out_req->WaitComplete(&usb_);
    zx_status_t in_status = in_req->WaitComplete(&usb_);
    status = out_status != ZX_OK ? out_status : in_status;
    if (status != ZX_OK) {
        return status;
    }

    void* out_data;
    status = usb_request_mmap(out_req->Get(), &out_data);
    if (status != ZX_OK) {
        return status;
    }
    void* in_data;
    status = usb_request_mmap(in_req->Get(), &in_data);
    if (status != ZX_OK) {
        return status;
    }
    return memcmp(in_data, out_data, params->len) == 0 ? ZX_OK : ZX_ERR_IO;
}

zx_status_t UsbTester::VerifyLoopback(const fbl::Vector<TestRequest>& out_reqs,
                                      const fbl::Vector<TestRequest>& in_reqs,
                                      size_t* out_num_passed) {
    size_t num_passed = 0;
    size_t next_out_idx = 0;
    for (auto& in_req : in_reqs) {
        usb_request_t* in_usb_req = in_req.Get();
        // You can't transfer an isochronous request of length zero.
        if (in_usb_req->response.status != ZX_OK || in_usb_req->response.actual == 0) {
            zxlogf(TRACE, "skipping isoch req, status %d, read len %lu\n",
                   in_usb_req->response.status, in_usb_req->response.actual);
            continue;
        }
        void* in_data;
        zx_status_t status = usb_request_mmap(in_usb_req, &in_data);
        if (status != ZX_OK) {
            return status;
        }
        // We will start searching the OUT requests from after the last matched OUT request to
        // preserve expected ordering.
        size_t out_idx = next_out_idx;
        bool matched = false;
        while (out_idx < out_reqs.size() && !matched) {
            auto& out_req = out_reqs[out_idx];
            usb_request_t* out_usb_req = out_req.Get();
            if (out_usb_req->response.status == ZX_OK &&
                out_usb_req->response.actual == in_usb_req->response.actual) {
                void* out_data;
                status = usb_request_mmap(out_usb_req, &out_data);
                if (status != ZX_OK) {
                    return status;
                }
                matched = memcmp(in_data, out_data, out_usb_req->response.actual) == 0;
            }
            out_idx++;
        }
        if (matched) {
            next_out_idx = out_idx;
            num_passed++;
        } else {
            // Maybe IN data was corrupted.
            zxlogf(TRACE, "could not find matching isoch req\n");
        }
    }
    *out_num_passed = num_passed;
    return ZX_OK;
}

zx_status_t UsbTester::IsochLoopback(const zircon_usb_tester_TestParams* params,
                                     zircon_usb_tester_IsochResult* result) {
    if (params->len > kReqMaxLen) {
        return ZX_ERR_INVALID_ARGS;
    }
    IsochLoopbackIntf* intf = &isoch_loopback_intf_;

    zx_status_t status = usb_set_interface(&usb_, intf->intf_num, intf->alt_setting);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_set_interface got err: %d\n", status);
        return status;
    }
    // TODO(jocelyndang): optionally allow the user to specify a packet size.
    uint16_t packet_size = fbl::min(intf->in_max_packet, intf->out_max_packet);
    size_t num_reqs = fbl::round_up(params->len, packet_size) / packet_size;

    zxlogf(TRACE, "allocating %lu reqs of packet size %u, total bytes %lu\n",
           num_reqs, packet_size, params->len);

    fbl::Vector<TestRequest> in_reqs;
    fbl::Vector<TestRequest> out_reqs;
    // We will likely get a few empty IN requests, as there is a delay between the start of an
    // OUT transfer and it being received. Allocate a few more IN requests to account for this.
    status = AllocTestReqs(num_reqs + kIsochAdditionalInReqs, packet_size, intf->in_addr,
                           &in_reqs);
    if (status != ZX_OK) {
        goto done;
    }
    status = AllocTestReqs(num_reqs, packet_size, intf->out_addr, &out_reqs);
    if (status != ZX_OK) {
        goto done;
    }
    status = FillTestReqs(out_reqs, params->data_pattern);
    if (status != ZX_OK) {
        goto done;
    }

    // Find the current frame so we can schedule OUT and IN requests to start simultaneously.
    uint64_t frame;
    frame = usb_get_current_frame(&usb_);
    // Adds some delay so we don't miss the scheduled start frame.
    uint64_t start_frame;
    start_frame = frame + kIsochStartFrameDelay;
    zxlogf(TRACE, "scheduling isoch loopback to start on frame %lu\n", start_frame);

    QueueTestReqs(in_reqs, start_frame);
    QueueTestReqs(out_reqs, start_frame);

    WaitTestReqs(out_reqs);
    WaitTestReqs(in_reqs);

    size_t num_passed;
    status = VerifyLoopback(out_reqs, in_reqs, &num_passed);
    if (status != ZX_OK) {
        goto done;
    }
    result->num_passed = num_passed;
    result->num_packets = num_reqs;
    zxlogf(TRACE, "%lu / %lu passed\n", num_passed, num_reqs);

done:
    zx_status_t res = usb_set_interface(&usb_, intf->intf_num, 0);
    if (res != ZX_OK) {
        zxlogf(ERROR, "could not switch back to isoch interface default alternate setting\n");
    }
    return status;
}

void UsbTester::GetVersion(uint8_t* major_version, uint8_t* minor_version) {
    usb_device_descriptor_t desc = {};
    usb_get_device_descriptor(&usb_, &desc);
    *major_version = MSB(desc.bcdDevice);
    *minor_version = LSB(desc.bcdDevice);
}

static zx_status_t fidl_SetModeFwloader(void* ctx, fidl_txn_t* txn) {
    auto* usb_tester = static_cast<UsbTester*>(ctx);
    return zircon_usb_tester_DeviceSetModeFwloader_reply(
        txn, usb_tester->SetModeFwloader());
}

static zx_status_t fidl_BulkLoopback(void* ctx, const zircon_usb_tester_TestParams* params,
                                     fidl_txn_t* txn) {
    auto* usb_tester = static_cast<UsbTester*>(ctx);
    return zircon_usb_tester_DeviceBulkLoopback_reply(
        txn, usb_tester->BulkLoopback(params));
}

static zx_status_t fidl_IsochLoopback(void* ctx, const zircon_usb_tester_TestParams* params,
                                      fidl_txn_t* txn) {
    auto* usb_tester = static_cast<UsbTester*>(ctx);
    zircon_usb_tester_IsochResult result = {};
    zx_status_t status = usb_tester->IsochLoopback(params, &result);
    return zircon_usb_tester_DeviceIsochLoopback_reply(txn, status, &result);
}

static zx_status_t fidl_GetVersion(void* ctx, fidl_txn_t* txn) {
    auto* usb_tester = static_cast<UsbTester*>(ctx);
    uint8_t major_version;
    uint8_t minor_version;
    usb_tester->GetVersion(&major_version, &minor_version);
    return zircon_usb_tester_DeviceGetVersion_reply(txn, major_version, minor_version);
}

static zircon_usb_tester_Device_ops_t fidl_ops = {
    .SetModeFwloader = fidl_SetModeFwloader,
    .BulkLoopback = fidl_BulkLoopback,
    .IsochLoopback = fidl_IsochLoopback,
    .GetVersion = fidl_GetVersion,
};

zx_status_t UsbTester::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return zircon_usb_tester_Device_dispatch(this, txn, msg, &fidl_ops);
}


zx_status_t UsbTester::Bind() {
    return DdkAdd("usb-tester");
}

// static
zx_status_t UsbTester::Create(zx_device_t* parent) {
    usb_protocol_t usb;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_USB, &usb);
    if (status != ZX_OK) {
        return status;
    }

    usb_composite_protocol_t usb_composite;
    status = device_get_protocol(parent, ZX_PROTOCOL_USB_COMPOSITE, &usb_composite);
    if (status != ZX_OK) {
        return status;
    }
    auto want_interface = [](usb_interface_descriptor_t* intf, void* arg) {
        return intf->bInterfaceClass == USB_CLASS_VENDOR;
    };
    status = usb_claim_additional_interfaces(&usb_composite, want_interface, nullptr);
    if (status != ZX_OK) {
        return status;
    }
    // Find the endpoints.
    usb_desc_iter_t iter;
    status = usb_desc_iter_init(&usb, &iter);
    if (status != ZX_OK) {
        return status;
    }

    uint8_t bulk_in_addr = 0;
    uint8_t bulk_out_addr = 0;
    IsochLoopbackIntf isoch_loopback_intf = {};

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, false);
    while (intf) {
        IsochLoopbackIntf isoch_intf = {};
        isoch_intf.intf_num = intf->bInterfaceNumber;
        isoch_intf.alt_setting = intf->bAlternateSetting;

        usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
        while (endp) {
            switch (usb_ep_type(endp)) {
            case USB_ENDPOINT_BULK:
                if (usb_ep_direction(endp) == USB_ENDPOINT_IN) {
                    bulk_in_addr = endp->bEndpointAddress;
                    zxlogf(TRACE, "usb_tester found bulk in ep: %x\n", bulk_in_addr);
                } else {
                    bulk_out_addr = endp->bEndpointAddress;
                    zxlogf(TRACE, "usb_tester found bulk out ep: %x\n", bulk_out_addr);
                }
                break;
            case USB_ENDPOINT_ISOCHRONOUS:
                if (usb_ep_direction(endp) == USB_ENDPOINT_IN) {
                    isoch_intf.in_addr = endp->bEndpointAddress;
                    isoch_intf.in_max_packet = usb_ep_max_packet(endp);
                } else {
                    isoch_intf.out_addr = endp->bEndpointAddress;
                    isoch_intf.out_max_packet = usb_ep_max_packet(endp);
                }
                break;
            }
            endp = usb_desc_iter_next_endpoint(&iter);
        }
        if (isoch_intf.in_addr && isoch_intf.out_addr) {
            // Found isoch loopback endpoints.
            isoch_loopback_intf = isoch_intf;
            zxlogf(TRACE, "usb tester found isoch loopback eps: %x (%u) %x (%u), intf %u %u\n",
                   isoch_intf.in_addr, isoch_intf.in_max_packet,
                   isoch_intf.out_addr, isoch_intf.out_max_packet,
                   isoch_intf.intf_num, isoch_intf.alt_setting);
        }
        intf = usb_desc_iter_next_interface(&iter, false);
    }
    usb_desc_iter_release(&iter);

    // Check we found the pair of bulk endpoints and isoch endpoints.
    if (!bulk_in_addr || !bulk_out_addr) {
        zxlogf(ERROR, "usb tester could not find bulk endpoints\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (!isoch_loopback_intf.in_addr || !isoch_loopback_intf.out_addr) {
        zxlogf(ERROR, "usb tester could not find isoch endpoints\n");
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<UsbTester> dev(
        new (&ac) UsbTester(parent, usb, bulk_in_addr, bulk_out_addr, isoch_loopback_intf));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = dev->Bind();
    if (status == ZX_OK) {
        // Intentionally leak as it is now held by DevMgr.
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

}  // namespace usb

extern "C" zx_status_t usb_tester_bind(void* ctx, zx_device_t* parent) {
    zxlogf(TRACE, "usb_tester_bind\n");
    return usb::UsbTester::Create(parent);
}
