// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <xdc-host-utils/conn.h>
#include <xdc-server-utils/msg.h>
#include <xdc-server-utils/packet.h>
#include <zircon/device/debug.h>

#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "usb-handler.h"
#include "xdc-server.h"

namespace xdc {

static constexpr uint32_t MAX_PENDING_CONN_BACKLOG = 128;
static const char* XDC_LOCK_PATH = "/tmp/xdc.lock";

void Client::SetStreamId(uint32_t stream_id) {
    registered_ = true;
    stream_id_ = stream_id;
}

void Client::SetConnected(bool connected) {
    if (connected == connected_) {
        fprintf(stderr, "tried to set client with stream id %u as %s again.\n",
                stream_id(), connected ? "connected" : "disconnected");
        return;
    }
    printf("client with stream id %u is now %s to the xdc device stream.\n",
           stream_id(), connected ? "connected" : "disconnected");
    connected_ = connected;
}

bool Client::UpdatePollState(bool usb_writable) {
    short updated_events = events_;
    // We want to poll for the client readable signal if:
    //  - The client has not yet registered a stream id, or,
    //  - The xdc stream of the client's id is ready to be written to.
    if (!stream_id() || (usb_writable && connected())) {
        updated_events |= POLLIN;
    } else {
        updated_events &= ~(POLLIN);
    }
    // We need to poll for the client writable signal if we have data to send to the client.
    if (completed_reads_.size() > 0) {
        updated_events |= POLLOUT;
    } else {
        updated_events &= ~POLLOUT;
    }
    if (updated_events != events_) {
        events_ = updated_events;
        return true;
    }
    return false;
}

void Client::AddCompletedRead(std::unique_ptr<UsbHandler::Transfer> transfer) {
    completed_reads_.push_back(std::move(transfer));
}

void Client::ProcessCompletedReads(const std::unique_ptr<UsbHandler>& usb_handler) {
    for (auto iter = completed_reads_.begin(); iter != completed_reads_.end();) {
        std::unique_ptr<UsbHandler::Transfer>& transfer = *iter;

        unsigned char* data = transfer->data() + transfer->offset();
        size_t len_to_write = transfer->actual_length() - transfer->offset();

        ssize_t total_written = 0;
        while (total_written < len_to_write) {
            ssize_t res = send(fd(), data + total_written, len_to_write - total_written, 0);
            if (res < 0) {
                if (errno == EAGAIN) {
                    fprintf(stderr, "can't send completed read to client currently\n");
                    // Need to wait for client to be writable again.
                    return;
                } else {
                    fprintf(stderr, "can't write to client, err: %s\n", strerror(errno));
                    return;
                }
            }
            total_written += res;
            int offset = transfer->offset() + res;
            assert(transfer->SetOffset(offset));
        }
        usb_handler->RequeueRead(std::move(transfer));
        iter = completed_reads_.erase(iter);
    }
}

zx_status_t Client::ProcessWrites(const std::unique_ptr<UsbHandler>& usb_handler) {
    if (!connected()) {
        return ZX_ERR_SHOULD_WAIT;
    }
    while (true) {
        if (!pending_write_) {
            pending_write_ = usb_handler->GetWriteTransfer();
            if (!pending_write_) {
                return ZX_ERR_SHOULD_WAIT; // No transfers currently available.
            }
        }
        // If there is no pending data to transfer, read more from the client.
        if (!has_write_data()) {
            // Read from the client into the usb transfer buffer. Leave space for the header.
            unsigned char* buf = pending_write_->write_data_buffer();

            int n = recv(fd(), buf, UsbHandler::Transfer::MAX_WRITE_DATA_SIZE, 0);
            if (n == 0) {
                return ZX_ERR_PEER_CLOSED;
            } else if (n == EAGAIN) {
                return ZX_ERR_SHOULD_WAIT;
            } else if (n < 0) {
                fprintf(stderr, "recv got unhandled err: %s\n", strerror(errno));
                return ZX_ERR_IO;
            }
            pending_write_->FillHeader(stream_id(), n);
        }
        if (usb_handler->writable()) {
            pending_write_ = usb_handler->QueueWriteTransfer(std::move(pending_write_));
            if (pending_write_) {
                // Usb handler was busy and returned the write.
                return ZX_ERR_SHOULD_WAIT;
            }
        } else {
            break; // Usb handler is busy, need to wait for some writes to complete.
        }
    }
    return ZX_ERR_SHOULD_WAIT;
}

void Client::ReturnTransfers(const std::unique_ptr<UsbHandler>& usb_handler) {
    for (auto& transfer : completed_reads_) {
        usb_handler->RequeueRead(std::move(transfer));
    }
    completed_reads_.clear();

    if (pending_write_) {
        usb_handler->ReturnWriteTransfer(std::move(pending_write_));
    }
}

// static
std::unique_ptr<XdcServer> XdcServer::Create() {
    auto conn = std::make_unique<XdcServer>(ConstructorTag{});
    if (!conn->Init()) {
        return nullptr;
    }
    return conn;
}

bool XdcServer::Init() {
    usb_handler_ = UsbHandler::Create();
    if (!usb_handler_) {
        return false;
    }

    socket_fd_.reset(socket(AF_UNIX, SOCK_STREAM, 0));
    if (!socket_fd_) {
        fprintf(stderr, "failed to create socket, err: %s\n", strerror(errno));
        return false;
    }
    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, XDC_SOCKET_PATH, sizeof(addr.sun_path));

    // Check if another instance of the xdc server is running.
    socket_lock_fd_.reset(open(XDC_LOCK_PATH, O_CREAT | O_RDONLY, 0666));
    if (!socket_lock_fd_) {
        return false;
    }
    int res = flock(socket_lock_fd_.get(), LOCK_EX | LOCK_NB);
    if (res != 0) {
        fprintf(stderr, "Failed to acquire socket lock, err: %s.\n", strerror(errno));
        return false;
    }
    // Remove the socket file if it exists.
    unlink(XDC_SOCKET_PATH);
    if (bind(socket_fd_.get(), (sockaddr*)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Could not bind socket with pathname: %s, err: %s\n",
                XDC_SOCKET_PATH, strerror(errno));
        return false;
    }
    if (listen(socket_fd_.get(), MAX_PENDING_CONN_BACKLOG) < 0) {
        fprintf(stderr, "Could not listen on socket fd: %d, err: %s\n",
                socket_fd_.get(), strerror(errno));
        return false;
    }
    return true;
}

void XdcServer::UpdateClientPollEvents() {
    for (auto iter : clients_) {
        std::shared_ptr<Client> client = iter.second;
        bool changed = client->UpdatePollState(usb_handler_->writable());
        if (changed) {
            // We need to update the corresponding file descriptor in the poll_fds_ array
            // passed to poll.
            int fd = client->fd();
            auto is_fd = [fd](auto& elem) { return elem.fd == fd; };
            auto fd_iter = std::find_if(poll_fds_.begin(), poll_fds_.end(), is_fd);
            if (fd_iter == poll_fds_.end()) {
                fprintf(stderr, "could not find pollfd for client with fd %d\n", fd);
                continue;
            }
            fd_iter->events = client->events();
        }
    }
}

void XdcServer::UpdateUsbHandlerFds() {
    std::map<int, short> added_fds;
    std::set<int> removed_fds;
    usb_handler_->GetFdUpdates(added_fds, removed_fds);

    for (auto iter : added_fds) {
        int fd = iter.first;
        short events = iter.second;

        auto match = std::find_if(poll_fds_.begin(), poll_fds_.end(),
                                  [&fd](auto& pollfd) { return pollfd.fd == fd; });
        if (match != poll_fds_.end()) {
            fprintf(stderr, "already have usb handler fd: %d\n", fd);
            continue;
        }
        poll_fds_.push_back(pollfd{fd, events, 0});
        printf("usb handler added fd: %d\n", fd);
    }
    for (auto fd : removed_fds) {
        auto match = std::remove_if(poll_fds_.begin(), poll_fds_.end(),
                                    [&fd](auto& pollfd) { return pollfd.fd == fd; });
        if (match == poll_fds_.end()) {
            fprintf(stderr, "could not find usb handler fd: %d to delete\n", fd);
            continue;
        }
        poll_fds_.erase(match, poll_fds_.end());
        printf("usb handler removed fd: %d\n", fd);
    }
}

void XdcServer::Run() {
    signal(SIGPIPE, SIG_IGN); // Prevent clients from causing SIGPIPE.

    printf("Waiting for connections on: %s\n", XDC_SOCKET_PATH);

    // Listen for new client connections.
    poll_fds_.push_back(pollfd{socket_fd_.get(), POLLIN, 0});

    // Initialize to true as we want to get the initial usb handler fds.
    bool update_usb_handler_fds = true;

    for (;;) {
        if (update_usb_handler_fds) {
            UpdateUsbHandlerFds();
            update_usb_handler_fds = false;
        }

        // poll expects an array of pollfds.
        int num = poll(&poll_fds_[0], poll_fds_.size(), -1 /* timeout */);
        if (num < 0) {
            fprintf(stderr, "poll failed, err: %s\n", strerror(errno));
            break;
        }
        // Not using an iterator for poll_fds_ as we might add/remove elements.
        int num_sockets = poll_fds_.size();
        int i = 0;
        while (i < num_sockets) {
            if (poll_fds_[i].fd == socket_fd_.get()) {
                // A new client is trying to connect.
                if (poll_fds_[i].revents & POLLIN) {
                    ClientConnect();
                    // Don't need to increment num_sockets as there aren't poll events for it yet.
                }
            } else if (usb_handler_->IsValidFd(poll_fds_[i].fd)) {
                if (poll_fds_[i].revents) {
                    std::vector<std::unique_ptr<UsbHandler::Transfer>> completed_reads;
                    update_usb_handler_fds = usb_handler_->HandleEvents(completed_reads);

                    SendQueuedCtrlMsgs();

                    for (auto& usb_transfer : completed_reads) {
                        UsbReadComplete(std::move(usb_transfer));
                    }
                }
            } else {
                auto iter = clients_.find(poll_fds_[i].fd);
                if (iter == clients_.end()) {
                    fprintf(stderr, "poll returned an unknown fd: %d\n", poll_fds_[i].fd);
                    poll_fds_.erase(poll_fds_.begin() + i);
                    --num_sockets;
                    continue;
                }

                std::shared_ptr<Client> client = iter->second;

                // Received client disconnect signal.
                // Only remove the client if the corresponding xdc device stream is offline.
                // Otherwise the client may still have data buffered to send to the usb handler,
                // and we will wait until reading from the client returns zero (disconnect).
                bool delete_client =
                    (poll_fds_[i].revents & POLLHUP) && !client->connected();

                // Check if the client had pending data to write, or signalled new data available.
                bool do_write = client->has_write_data() && usb_handler_->writable() &&
                                client->connected();
                bool new_data_available = poll_fds_[i].revents & POLLIN;
                if (!delete_client && (do_write || new_data_available)) {
                    if (!client->registered()) {
                        // Delete the client if registering the stream failed.
                        delete_client = !RegisterStream(client);
                    }
                    if (!delete_client) {
                        zx_status_t status = client->ProcessWrites(usb_handler_);
                        if (status == ZX_ERR_PEER_CLOSED) {
                            delete_client = true;
                        }
                    }
                }

                if (delete_client) {
                    client->ReturnTransfers(usb_handler_);
                    // Notify the host server that the stream is now offline.
                    if (client->stream_id()) {
                        NotifyStreamState(client->stream_id(), false /* online */);
                    }
                    poll_fds_.erase(poll_fds_.begin() + i);
                    --num_sockets;
                    printf("fd %d stream %u disconnected\n", client->fd(), client->stream_id());
                    clients_.erase(iter);
                    continue;
                }
                client->ProcessCompletedReads(usb_handler_);
            }
            ++i;
        }
        UpdateClientPollEvents();
    }
}

void XdcServer::ClientConnect() {
    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);
    // Most of the time we want non-blocking transfers, so we can handle other clients / libusb.
    int client_fd = accept(socket_fd_.get(), (struct sockaddr*)&addr, &len);
    if (client_fd < 0) {
        fprintf(stderr, "Socket accept failed, err: %s\n", strerror(errno));
        return;
    }
    if (clients_.count(client_fd) > 0) {
        fprintf(stderr, "Client already connected, socket fd: %d\n", client_fd);
        return;
    }
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags < 0) {
        fprintf(stderr, "Could not get socket flags, err: %s\n", strerror(errno));
        close(client_fd);
        return;
    }
    int res = fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    if (res != 0) {
        fprintf(stderr, "Could not set socket as nonblocking, err: %s\n", strerror(errno));
        close(client_fd);
        return;
    }
    printf("Client connected, socket fd: %d\n", client_fd);
    clients_[client_fd] = std::make_shared<Client>(client_fd);
    poll_fds_.push_back(pollfd{client_fd, POLLIN, 0});
}

bool XdcServer::RegisterStream(std::shared_ptr<Client> client) {
    RegisterStreamRequest stream_id;
    ssize_t n = recv(client->fd(), &stream_id, sizeof(stream_id), MSG_WAITALL);
    if (n != sizeof(stream_id)) {
        fprintf(stderr, "failed to read stream id from client fd: %d, got len: %ld, got err: %s\n",
                client->fd(), n, strerror(errno));
        return false;
    }
    // Client has disconnected. This will be handled in the main poll thread.
    if (n == 0) {
        return false;
    }
    RegisterStreamResponse resp = false;
    if (stream_id == DEBUG_STREAM_ID_RESERVED) {
        fprintf(stderr, "cannot register stream id %u\n", DEBUG_STREAM_ID_RESERVED);
    } else if (GetClient(stream_id)) {
        fprintf(stderr, "stream id %u was already registered\n", stream_id);
    } else {
        client->SetStreamId(stream_id);
        printf("registered stream id %u\n", stream_id);
        NotifyStreamState(stream_id, true /* online */);
        if (dev_stream_ids_.count(stream_id)) {
            client->SetConnected(true);
        }
        resp = true;
    }

    ssize_t res = send(client->fd(), &resp, sizeof(resp), MSG_WAITALL);
    if (res != sizeof(resp)) {
        // Failed to send reply, disconnect the client.
        return false;
    }
    return resp;
}

std::shared_ptr<Client> XdcServer::GetClient(uint32_t stream_id) {
    auto is_client = [stream_id](auto& pair) -> bool {
        return pair.second->stream_id() == stream_id;
    };
    auto iter = std::find_if(clients_.begin(), clients_.end(), is_client);
    return iter == clients_.end() ? nullptr : iter->second;
}

void XdcServer::UsbReadComplete(std::unique_ptr<UsbHandler::Transfer> transfer) {
    auto requeue = fit::defer([&]() { usb_handler_->RequeueRead(std::move(transfer)); });

    bool is_new_packet;
    uint32_t stream_id;

    zx_status_t status = xdc_update_packet_state(&read_packet_state_, transfer->data(),
                                                 transfer->actual_length(), &is_new_packet);
    if (status != ZX_OK) {
        fprintf(stderr, "error processing transfer: %d, dropping read of size %d\n",
                status, transfer->actual_length());
    }
    stream_id = read_packet_state_.header.stream_id;
    if (is_new_packet && stream_id == XDC_MSG_STREAM) {
        HandleCtrlMsg(transfer->data(), transfer->actual_length());
        return;
    }
    // Pass the completed transfer to the registered client, if any.
    auto client = GetClient(stream_id);
    if (!client) {
        fprintf(stderr, "No client registered for stream %u, dropping read of size %d\n",
                stream_id, transfer->actual_length());
        return;
    }
    // If it is the start of a new packet, the client should begin reading after the header.
    int offset = is_new_packet ? sizeof(xdc_packet_header_t) : 0;
    assert(transfer->SetOffset(offset));
    client->AddCompletedRead(std::move(transfer));
    requeue.cancel();
}

void XdcServer::HandleCtrlMsg(unsigned char* transfer_buf, int transfer_len) {
    int data_len = transfer_len - (int)sizeof(xdc_packet_header_t);
    if (data_len < (int)sizeof(xdc_msg_t)) {
        fprintf(stderr, "malformed msg, got %d bytes, need %lu\n", data_len, sizeof(xdc_msg_t));
        return;
    }
    xdc_msg_t* msg = reinterpret_cast<xdc_msg_t*>(transfer_buf + sizeof(xdc_packet_header_t));
    switch (msg->opcode) {
    case XDC_NOTIFY_STREAM_STATE: {
        uint32_t stream_id = msg->notify_stream_state.stream_id;
        bool online = msg->notify_stream_state.online;

        auto dev_stream = dev_stream_ids_.find(stream_id);
        bool saved_online_state = dev_stream != dev_stream_ids_.end();
        if (online == saved_online_state) {
            fprintf(stderr, "tried to set stream %u to %s again\n",
                    stream_id, online ? "online" : "offline");
            return;
        }
        if (online) {
            dev_stream_ids_.insert(stream_id);
        } else {
            dev_stream_ids_.erase(dev_stream);
        }
        printf("xdc device stream id %u is now %s\n", stream_id, online ? "online" : "offline");

        // Update the host client's connected status.
        auto client = GetClient(stream_id);
        if (!client) {
            break;
        }
        client->SetConnected(online);
        break;
    }
    default:
        fprintf(stderr, "unknown msg opcode: %u\n", msg->opcode);
    }
}

void XdcServer::NotifyStreamState(uint32_t stream_id, bool online) {
    xdc_msg_t msg = {
        .opcode = XDC_NOTIFY_STREAM_STATE,
        .notify_stream_state.stream_id = stream_id,
        .notify_stream_state.online = online};
    queued_ctrl_msgs_.push_back(msg);
    SendQueuedCtrlMsgs();
}

bool XdcServer::SendCtrlMsg(xdc_msg_t& msg) {
    std::unique_ptr<UsbHandler::Transfer> transfer = usb_handler_->GetWriteTransfer();
    if (!transfer) {
        return false;
    }
    zx_status_t res = transfer->FillData(DEBUG_STREAM_ID_RESERVED,
                                         reinterpret_cast<unsigned char*>(&msg), sizeof(msg));
    assert(res == ZX_OK); // Should not fail.
    transfer = usb_handler_->QueueWriteTransfer(std::move(transfer));
    bool queued = !transfer;
    if (!queued) {
        usb_handler_->ReturnWriteTransfer(std::move(transfer));
    }
    return queued;
}

void XdcServer::SendQueuedCtrlMsgs() {
    auto msgs_iter = queued_ctrl_msgs_.begin();
    while (msgs_iter != queued_ctrl_msgs_.end()) {
        bool sent = SendCtrlMsg(*msgs_iter);
        if (sent) {
            msgs_iter = queued_ctrl_msgs_.erase(msgs_iter);
        } else {
            // Need to wait.
            return;
        }
    }
}

} // namespace xdc

int main(int argc, char** argv) {
    printf("Starting XHCI Debug Capability server...\n");
    std::unique_ptr<xdc::XdcServer> xdc_server = xdc::XdcServer::Create();
    if (!xdc_server) {
        return -1;
    }
    xdc_server->Run();
    return 0;
}
