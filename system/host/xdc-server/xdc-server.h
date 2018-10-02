// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_fd.h>
#include <map>
#include <poll.h>
#include <set>
#include <vector>

#include "usb-handler.h"

namespace xdc {

class Client {
public:
    explicit Client(int fd)
        : fd_(fd) {}

    void SetStreamId(uint32_t stream_id);
    void SetConnected(bool connected);
    // Returns whether the set of client events we are polling for has changed.
    bool UpdatePollState(bool usb_writable);

    // Queues a completed read transfer to be written to the client.
    void AddCompletedRead(std::unique_ptr<UsbHandler::Transfer> transfer);
    // Writes data from completed read transfers to the client until there are
    // no transfers left, or the client is currently unavailable to accept data.
    void ProcessCompletedReads(const std::unique_ptr<UsbHandler>& usb_handler);
    // Writes data read from the client to the usb handler.
    zx_status_t ProcessWrites(const std::unique_ptr<UsbHandler>& usb_handler);
    // Returns any unused transfers to the usb handler.
    void ReturnTransfers(const std::unique_ptr<UsbHandler>& usb_handler);

    int fd() const { return fd_.get(); }
    // Returns the set of client events we should poll for.
    short events() const { return events_; }
    bool registered() const { return registered_; }
    uint32_t stream_id() const { return stream_id_; }
    bool connected() const { return connected_; }
    // Returns true if we have read data from the client not yet sent to the usb handler.
    bool has_write_data() const { return pending_write_ && pending_write_->request_length() > 0; }

private:
    fbl::unique_fd fd_;

    short events_ = 0;

    // Whether the client has registered a stream id.
    bool registered_ = false;
    uint32_t stream_id_ = 0;
    // True if the client has registered a stream id,
    // and that stream id is also registered on the xdc device side.
    bool connected_ = false;

    std::vector<std::unique_ptr<UsbHandler::Transfer>> completed_reads_;
    // Data read from the client, to be sent to the usb handler.
    std::unique_ptr<UsbHandler::Transfer> pending_write_;
};

class XdcServer {
    // This is required by the XdcServer constructor, to stop clients calling it directly.
    struct ConstructorTag {
        explicit ConstructorTag() = default;
    };

public:
    // Create should be called instead. This is public for make_unique.
    XdcServer(ConstructorTag tag) {}

    static std::unique_ptr<XdcServer> Create();
    void Run();

private:
    bool Init();

    void UpdateClientPollEvents();

    // Updates poll_fds_ with any newly added or removed usb handler fds.
    void UpdateUsbHandlerFds();

    // Processes new client connections on the server socket.
    void ClientConnect();

    // Returns whether registration succeeded.
    bool RegisterStream(std::shared_ptr<Client> client);

    // Returns the client registered to the given stream id, or nullptr if none was foumd.
    std::shared_ptr<Client> GetClient(uint32_t stream_id);

    void UsbReadComplete(std::unique_ptr<UsbHandler::Transfer> transfer);
    // Parses the control message from the given transfer buffer.
    void HandleCtrlMsg(unsigned char* transfer_buf, int transfer_len);

    // Sends a control message the to the xdc device with whether a stream has gone on / offline.
    // If the message cannot currently be sent, it is queued to be retried later.
    void NotifyStreamState(uint32_t stream_id, bool online);
    bool SendCtrlMsg(xdc_msg_t& msg);
    void SendQueuedCtrlMsgs();

    std::unique_ptr<UsbHandler> usb_handler_;

    // Server socket we receive client connections on.
    fbl::unique_fd socket_fd_;
    // File lock acquired to ensure we don't unlink the socket of a running xdc server instance.
    fbl::unique_fd socket_lock_fd_;

    // Maps from client socket file descriptor to client.
    std::map<int, std::shared_ptr<Client>> clients_;

    // File descriptors we are currently polling on.
    std::vector<pollfd> poll_fds_;

    // Stream ids registered on the xdc device side.
    std::set<uint32_t> dev_stream_ids_;

    std::vector<xdc_msg_t> queued_ctrl_msgs_;

    xdc_packet_state_t read_packet_state_;
};

} // namespace xdc
