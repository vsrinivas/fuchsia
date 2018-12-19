// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-tester.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/usb/composite.h>
#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>
#include <usb/usb-request.h>
#include <lib/sync/completion.h>
#include <zircon/hw/usb.h>
#include <zircon/usb/tester/c/fidl.h>

#include <optional>
#include <stdlib.h>
#include <string.h>
#include <utility>

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

std::optional<TestRequest> TestRequest::Create(size_t len, uint8_t ep_address, size_t req_size) {
    usb_request_t* usb_req;
    zx_status_t status = usb_request_alloc(&usb_req, len, ep_address, req_size);
    if (status != ZX_OK) {
        return std::nullopt;
    }
    return TestRequest(usb_req);
}

std::optional<TestRequest> TestRequest::Create(const zircon_usb_tester_SgList& sg_list,
                                               uint8_t ep_address, size_t req_size) {
    size_t buffer_size = 0;
    // We need to allocate a usb request buffer that covers all the scatter gather entries.
    for (uint64_t i = 0; i < sg_list.len; ++i) {
        auto& entry = sg_list.entries[i];
        buffer_size = fbl::max(buffer_size, entry.offset + entry.length);
    }
    usb_request_t* usb_req;
    zx_status_t status = usb_request_alloc(&usb_req, buffer_size, ep_address, req_size);
    if (status != ZX_OK) {
        return std::nullopt;
    }
    // Convert the scatter gather list from fidl format to phys_iter format.
    // usb_request_set_sg_list copies the provided array, so we can declare this locally.
    phys_iter_sg_entry_t phys_iter_sg_list[sg_list.len];
    for (uint64_t i = 0; i < sg_list.len; ++i) {
        const auto& entry = sg_list.entries[i];
        phys_iter_sg_list[i] = { .length = entry.length, .offset = entry.offset };
    }
    status = usb_request_set_sg_list(usb_req, phys_iter_sg_list, sg_list.len);
    if (status != ZX_OK) {
        usb_request_release(usb_req);
        return std::nullopt;
    }
    return TestRequest(usb_req);
}

TestRequest::TestRequest(usb_request_t* usb_req) : usb_req_(usb_req) {
}

TestRequest::~TestRequest() {
    if (usb_req_) {
        usb_request_release(usb_req_);
    }
}

void TestRequest::RequestCompleteCallback(void* ctx, usb_request_t* request) {
    ZX_DEBUG_ASSERT(ctx != nullptr);
    sync_completion_signal(reinterpret_cast<sync_completion_t*>(ctx));
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
    const auto* sg_list = Get()->sg_list;
    uint64_t sg_count = Get()->sg_count;
    // If there is no scatter gather list, we can use this temporary entry for the whole request..
    phys_iter_sg_entry_t default_sg_list = { .length = Get()->header.length, .offset = 0 };
    if (!sg_list) {
        sg_list = &default_sg_list;
        sg_count = 1;
    }

    for (size_t i = 0; i < sg_count; ++i) {
        const auto& sg_entry = sg_list[i];
        for (size_t j = 0; j < sg_entry.length; ++j) {
            uint8_t* elem = buf + sg_entry.offset + j;
            switch (data_pattern) {
            case zircon_usb_tester_DataPatternType_CONSTANT:
                *elem = kTestDummyData;
                break;
            case zircon_usb_tester_DataPatternType_RANDOM:
                *elem = static_cast<uint8_t>(rand() % 256);
                break;
            default:
                return ZX_ERR_INVALID_ARGS;
             }
        }
    }
    return ZX_OK;
}

zx_status_t UsbTester::AllocTestReqs(size_t num_reqs, size_t len, uint8_t ep_addr,
                                     fbl::Vector<TestRequest>* out_test_reqs, size_t req_size) {

    fbl::AllocChecker ac;
    out_test_reqs->reserve(num_reqs, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    for (size_t i = 0; i < num_reqs; ++i) {
        auto test_req = TestRequest::Create(len, ep_addr, req_size);
        if (!test_req.has_value()) {
            return ZX_ERR_NO_MEMORY;
        }
        out_test_reqs->push_back(std::move(test_req.value()));
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
        usb_request_queue(&usb_, usb_req, test_req.GetCompleteCb());
    }
}

zx_status_t UsbTester::SetModeFwloader() {
    zx_status_t status = usb_control_out(&usb_,
                                         USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                         USB_TESTER_SET_MODE_FWLOADER, 0, 0, ZX_SEC(kReqTimeoutSecs),
                                         nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to set mode fwloader, err: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx_status_t TestRequest::GetDataUnscattered(fbl::Array<uint8_t>* out_data) {
    size_t len = Get()->response.actual;
    fbl::AllocChecker ac;
    fbl::Array<uint8_t> buf(new (&ac) uint8_t[len], len);

    if (!ac.check()) {
        zxlogf(ERROR, "GetDataUnscattered: could not allocate buffer of size %lu\n", len);
        return ZX_ERR_NO_MEMORY;
    }

    uint8_t* req_data;
    zx_status_t status = usb_request_mmap(Get(), reinterpret_cast<void**>(&req_data));
    if (status != ZX_OK) {
        return status;
    }
    if (Get()->sg_list) {
        size_t total_copied = 0;
        for (size_t i = 0; i < Get()->sg_count; ++i) {
            const auto& entry = Get()->sg_list[i];
            size_t len_to_copy = fbl::min(len - total_copied, entry.length);
            memcpy(buf.get() + total_copied, req_data + entry.offset, len_to_copy);
            total_copied += len_to_copy;
        }
    } else {
        memcpy(buf.get(), req_data, len);
    }

    *out_data = std::move(buf);
    return ZX_OK;
}

zx_status_t UsbTester::BulkLoopback(const zircon_usb_tester_TestParams* params,
                                    const zircon_usb_tester_SgList* out_sg_list,
                                    const zircon_usb_tester_SgList* in_sg_list) {
    if (params->len > kReqMaxLen) {
        return ZX_ERR_INVALID_ARGS;
    }
    auto out_req = out_sg_list ?
        TestRequest::Create(*out_sg_list, bulk_out_addr_, parent_req_size_) :
        TestRequest::Create(params->len, bulk_out_addr_, parent_req_size_);
    if (!out_req.has_value()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto in_req = in_sg_list ?
        TestRequest::Create(*in_sg_list, bulk_in_addr_, parent_req_size_) :
        TestRequest::Create(params->len, bulk_in_addr_, parent_req_size_);
    if (!in_req.has_value()) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = out_req->FillData(params->data_pattern);
    if (status != ZX_OK) {
        return status;
    }
    usb_request_queue(&usb_, out_req->Get(), out_req->GetCompleteCb());
    usb_request_queue(&usb_, in_req->Get(), in_req->GetCompleteCb());

    zx_status_t out_status = out_req->WaitComplete(&usb_);
    zx_status_t in_status = in_req->WaitComplete(&usb_);
    status = out_status != ZX_OK ? out_status : in_status;
    if (status != ZX_OK) {
        return status;
    }

    fbl::Array<uint8_t> out_data;
    status = out_req->GetDataUnscattered(&out_data);
    if (status != ZX_OK) {
        return status;
    }
    fbl::Array<uint8_t> in_data;
    status = in_req->GetDataUnscattered(&in_data);
    if (status != ZX_OK) {
        return status;
    }
    if (out_data.size() != params->len || in_data.size() != params->len) {
        return false;
    }
    return memcmp(in_data.get(), out_data.get(), params->len) == 0 ? ZX_OK : ZX_ERR_IO;
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
                           &in_reqs, parent_req_size_);
    if (status != ZX_OK) {
        goto done;
    }
    status = AllocTestReqs(num_reqs, packet_size, intf->out_addr, &out_reqs, parent_req_size_);
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
                                     const zircon_usb_tester_SgList* out_sg_list,
                                     const zircon_usb_tester_SgList* in_sg_list,
                                     fidl_txn_t* txn) {
    auto* usb_tester = static_cast<UsbTester*>(ctx);
    return zircon_usb_tester_DeviceBulkLoopback_reply(
        txn, usb_tester->BulkLoopback(params, out_sg_list, in_sg_list));
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
    size_t parent_req_size = usb_get_request_size(&usb);

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
            usb_ss_ep_comp_descriptor_t* ss_comp_desc = NULL;
            usb_descriptor_header_t* desc = usb_desc_iter_peek(&iter);
            if (desc && desc->bDescriptorType == USB_DT_SS_EP_COMPANION) {
                ss_comp_desc = (usb_ss_ep_comp_descriptor_t *)desc;
            }
            status = usb_enable_endpoint(&usb, endp, ss_comp_desc, true);
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s: usb_enable_endpoint failed %d\n", __FUNCTION__, status);
                return status;
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
        new (&ac) UsbTester(parent, usb, bulk_in_addr, bulk_out_addr, isoch_loopback_intf, parent_req_size));
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

extern "C" zx_status_t usb_tester_bind(void* ctx, zx_device_t* parent) {
    zxlogf(TRACE, "usb_tester_bind\n");
    return usb::UsbTester::Create(parent);
}

static zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = usb_tester_bind;
    return ops;
}();

}  // namespace usb

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_tester, usb::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_DEVICE),
    BI_ABORT_IF(NE, BIND_USB_VID, GOOGLE_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, USB_TESTER_PID),
ZIRCON_DRIVER_END(usb_tester)
// clang-format on
