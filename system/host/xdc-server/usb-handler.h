// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <xdc-server-utils/packet.h>
#include <zircon/types.h>

#include <map>
#include <set>
#include <vector>

namespace xdc {

class UsbHandler {
    // This is required by the UsbHandler constructor, to stop clients calling it directly.
    struct ConstructorTag {
        explicit ConstructorTag() = default;
    };

public:
    class Transfer {
    public:
        static constexpr const size_t BUFFER_SIZE = 16 * 1024;
        static constexpr const size_t HEADER_SIZE = sizeof(xdc_packet_header_t);

        static constexpr const size_t MAX_WRITE_DATA_SIZE = BUFFER_SIZE - HEADER_SIZE;

        // Create should be called instead. This is public for make_shared.
        explicit Transfer(ConstructorTag tag) {}

        // Sets the header of the transfer.
        // Returns ZX_OK on success, or ZX_ERR_INVALID_ARGS if data_len is larger than
        // MAX_WRITE_DATA_SIZE.
        zx_status_t FillHeader(uint32_t stream_id, size_t data_len);

        // Sets the contents of the transfer.
        // Returns ZX_OK on success, or ZX_ERR_INVALID_ARGS if data_len is larger than
        // MAX_WRITE_DATA_SIZE.
        zx_status_t FillData(uint32_t stream_id, unsigned char* data, size_t data_len);

        bool SetOffset(int offset);

        // Returns the data buffer to be populated for a write transfer.
        unsigned char* write_data_buffer() const { return data_ + HEADER_SIZE; }
        unsigned char* data() const { return data_; }
        // The number of bytes to be transferred.
        int request_length() const { return request_length_; }
        // The number of bytes successfully transferred.
        int actual_length() const { return actual_length_; }
        // Returns where the client has read up to in the data.
        // An offset equal to actual_length indicates the client has reached the end.
        int offset() const { return offset_; }

    private:
        // Only UsbHandler should create transfers.
        static std::unique_ptr<Transfer> Create();

        // TODO(jocelyndang): this should store a libusb_transfer instead.
        unsigned char* data_;
        int request_length_;
        int actual_length_;

        int offset_;

        friend class UsbHandler;
    };

    // Create should be called instead. This is public for make_unique.
    explicit UsbHandler(ConstructorTag tag) {}

    static std::unique_ptr<UsbHandler> Create();

    // Handles any pending events.
    //
    // Parameters:
    // completed_reads  A vector which will be populated with the usb transfers containing data
    //                  read from the xdc device. Once the client has finished processing a read,
    //                  it should be returned back to the UsbHandler by calling RequeueRead.
    //
    // Returns whether the usb handler fds have changed.
    // If true, the newly added or removed fds should be fetched via GetFdUpdates.
    bool HandleEvents(std::vector<std::unique_ptr<Transfer>>& completed_reads);

    // Returns the read transfer back to the UsbHandler to be requeued.
    void RequeueRead(std::unique_ptr<Transfer> transfer);

    // Populates added_fds and removed_fds with the fds that have been added
    // and removed since GetFdUpdates was last called.
    //
    // Parameters:
    // added_fds      A map that will be populated with fds to start monitoring and
    //                the corresponding events to monitor for.
    // removed_fds    A set that will be populated with fds to stop monitoring.
    //                The fds will be disjoint from added_fds.
    void GetFdUpdates(std::map<int, short>& added_fds, std::set<int>& removed_fds);

    // Returns a write transfer that can be used with QueueWriteTransfer to write
    // data to the xdc device. May return a nullptr if no transfers are available.
    std::unique_ptr<Transfer> GetWriteTransfer();
    void ReturnWriteTransfer(std::unique_ptr<Transfer>);
    // Returns a nullptr if the transfer was successfully queued,
    // otherwise returns the transfer to the client.
    std::unique_ptr<Transfer> QueueWriteTransfer(std::unique_ptr<Transfer>);

    // Returns whether the given file descriptor is currently valid for the usb handler.
    bool IsValidFd(int fd) const { return fds_.count(fd); }

    bool writable() const { return writable_; }

private:
    // All the libusb fds.
    std::set<int> fds_;

    bool writable_;
};

} // namespace xdc
