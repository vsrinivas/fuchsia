// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-handler.h"

namespace xdc {

// static
std::unique_ptr<UsbHandler::Transfer> UsbHandler::Transfer::Create() {
    auto transfer = std::make_unique<UsbHandler::Transfer>(ConstructorTag{});
    // TODO(jocelyndang): need to initialize the transfer.
    return transfer;
}

zx_status_t UsbHandler::Transfer::FillHeader(uint32_t stream_id, size_t data_len) {
    // TODO(jocelyndang): implement this.
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbHandler::Transfer::FillData(uint32_t stream_id, unsigned char* data,
                                           size_t data_len) {
    // TODO(jocelyndang): implement this.
    return ZX_ERR_NOT_SUPPORTED;
}

bool UsbHandler::Transfer::SetOffset(int offset) {
    if (offset < 0 || offset > actual_length_) {
        return false;
    }
    offset_ = offset;
    return true;
}

// static
std::unique_ptr<UsbHandler> UsbHandler::Create() {
    auto usb_handler = std::make_unique<UsbHandler>(ConstructorTag{});
    if (!usb_handler) {
        return nullptr;
    }
    // TODO(jocelyndang): need to initialize initial libusb fds.
    return usb_handler;
}

bool UsbHandler::HandleEvents(std::vector<std::unique_ptr<Transfer>>& completed_reads) {
    // TODO(jocelyndang): implement this.
    return false;
}

void UsbHandler::RequeueRead(std::unique_ptr<Transfer> transfer) {
    // TODO(jocelyndang): implement this.
}

void UsbHandler::GetFdUpdates(std::map<int, short>& added_fds, std::set<int>& removed_fds) {
    // TODO(jocelyndang): implement this.
}

std::unique_ptr<UsbHandler::Transfer> UsbHandler::GetWriteTransfer() {
    // TODO(jocelyndang): implement this.
    return nullptr;
}

void UsbHandler::ReturnWriteTransfer(std::unique_ptr<Transfer>) {
    // TODO(jocelyndang): implement this.
}

std::unique_ptr<UsbHandler::Transfer> UsbHandler::QueueWriteTransfer(
    std::unique_ptr<Transfer> transfer) {
    // TODO(jocelyndang): implement this.
    return transfer;
}

} // namespace xdc