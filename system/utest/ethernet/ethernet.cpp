// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/compiler.h>
#include <magenta/device/device.h>
#include <magenta/device/ethernet.h>
#include <magenta/device/ethertap.h>
#include <magenta/status.h>
#include <magenta/types.h>
#include <mx/fifo.h>
#include <mx/socket.h>
#include <mx/time.h>
#include <mx/vmar.h>
#include <mx/vmo.h>
#include <mxio/watcher.h>
#include <fbl/auto_call.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <unittest/unittest.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

const char kEthernetDir[] = "/dev/class/ethernet";
const char kTapctl[] = "/dev/misc/tapctl";
const char kTapDevName[] = "test";
const uint8_t kTapMac[] = { 0x12, 0x20, 0x30, 0x40, 0x50, 0x60 };

const char* mxstrerror(mx_status_t status) {
    return mx_status_get_string(status);
}

mx_status_t CreateEthertap(uint32_t mtu, mx::socket* sock) {
    if (sock == nullptr) {
        return MX_ERR_INVALID_ARGS;
    }

    int ctlfd = open(kTapctl, O_RDONLY);
    if (ctlfd < 0) {
        fprintf(stderr, "could not open %s: %s\n", kTapctl, strerror(errno));
        return MX_ERR_IO;
    }
    auto closer = fbl::MakeAutoCall([ctlfd]() { close(ctlfd); });

    ethertap_ioctl_config_t config = {};
    strlcpy(config.name, kTapDevName, ETHERTAP_MAX_NAME_LEN);
    // Uncomment this to trace ETHERTAP events
    //config.options = ETHERTAP_OPT_TRACE;
    config.mtu = mtu;
    memcpy(config.mac, kTapMac, 6);

    ssize_t rc = ioctl_ethertap_config(ctlfd, &config, sock->reset_and_get_address());
    if (rc < 0) {
        mx_status_t status = static_cast<mx_status_t>(rc);
        fprintf(stderr, "could not configure ethertap device: %s\n", mxstrerror(status));
        return status;
    }
    return MX_OK;
}

mx_status_t WatchCb(int dirfd, int event, const char* fn, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) return MX_OK;
    if (!strcmp(fn, ".") || !strcmp(fn, "..")) return MX_OK;

    int devfd = openat(dirfd, fn, O_RDONLY);
    if (devfd < 0) {
        return MX_OK;
    }
    auto closer = fbl::MakeAutoCall([devfd]() { close(devfd); });

    // See if this device is our ethertap device
    eth_info_t info;
    ssize_t rc = ioctl_ethernet_get_info(devfd, &info);
    if (rc < 0) {
        mx_status_t status = static_cast<mx_status_t>(rc);
        fprintf(stderr, "could not get ethernet info for %s/%s: %s\n", kEthernetDir, fn,
                        mxstrerror(status));
        // Return MX_OK to keep watching for devices.
        return MX_OK;
    }
    if (!(info.features & ETH_FEATURE_SYNTH)) {
        // Not a match, keep looking.
        return MX_OK;
    }

    // Found it!
    // TODO(tkilbourn): this might not be the test device we created; need a robust way of getting
    // the name of the tap device to check. Note that ioctl_device_get_device_name just returns
    // "ethernet" since that's the child of the tap device that we've opened here.
    auto fd = reinterpret_cast<int*>(cookie);
    *fd = devfd;
    closer.cancel();
    return MX_ERR_STOP;
}

mx_status_t OpenEthertapDev(int* fd) {
    if (fd == nullptr) {
        return MX_ERR_INVALID_ARGS;
    }

    int ethdir = open(kEthernetDir, O_RDONLY);
    if (ethdir < 0) {
        fprintf(stderr, "could not open %s: %s\n", kEthernetDir, strerror(errno));
        return MX_ERR_IO;
    }

    mx_status_t status = mxio_watch_directory(ethdir, WatchCb, mx::deadline_after(MX_SEC(2)),
                                              reinterpret_cast<void*>(fd));
    if (status == MX_ERR_STOP) {
        return MX_OK;
    } else {
        return status;
    }
}

struct FifoEntry : public fbl::SinglyLinkedListable<fbl::unique_ptr<FifoEntry>> {
    eth_fifo_entry_t e;
};

class EthernetClient {
  public:
    explicit EthernetClient(int fd) : fd_(fd) {}
    ~EthernetClient() {
        if (mapped_ > 0) {
            mx::vmar::root_self().unmap(mapped_, vmo_size_);
        }
        close(fd_);
    }

    mx_status_t Register(const char* name, uint32_t nbufs, uint16_t bufsize) {
        ssize_t rc = ioctl_ethernet_set_client_name(fd_, name, strlen(name) + 1);
        if (rc < 0) {
            return static_cast<mx_status_t>(rc);
        }

        eth_fifos_t fifos;
        rc = ioctl_ethernet_get_fifos(fd_, &fifos);
        if (rc < 0) {
            return static_cast<mx_status_t>(rc);
        }

        tx_.reset(fifos.tx_fifo);
        rx_.reset(fifos.rx_fifo);
        tx_depth_ = fifos.tx_depth;
        rx_depth_ = fifos.rx_depth;

        nbufs_ = nbufs;
        bufsize_ = bufsize;

        vmo_size_ = 2 * nbufs_ * bufsize_;
        mx_status_t status = mx::vmo::create(vmo_size_, 0u, &buf_);
        if (status != MX_OK) {
            return status;
        }

        status = mx::vmar::root_self().map(0, buf_, 0, vmo_size_,
                                           MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                                           &mapped_);
        if (status != MX_OK) {
            return status;
        }

        mx::vmo buf_copy;
        status = buf_.duplicate(MX_RIGHT_SAME_RIGHTS, &buf_copy);
        if (status != MX_OK) {
            return status;
        }

        mx_handle_t bufh = buf_copy.release();
        rc = ioctl_ethernet_set_iobuf(fd_, &bufh);
        if (rc < 0) {
            return static_cast<mx_status_t>(rc);
        }

        uint32_t idx = 0;
        for (; idx < nbufs; idx++) {
            eth_fifo_entry_t entry = {
                .offset = idx * bufsize_,
                .length = bufsize_,
                .flags = 0,
                .cookie = nullptr,
            };
            uint32_t actual;
            status = rx_.write(&entry, sizeof(entry), &actual);
            if (status != MX_OK) {
                return status;
            }
        }

        for (; idx < 2 * nbufs; idx++) {
            auto entry = fbl::unique_ptr<FifoEntry>(new FifoEntry);
            entry->e.offset = idx * bufsize_;
            entry->e.length = bufsize_;
            entry->e.flags = 0;
            entry->e.cookie = reinterpret_cast<uint8_t*>(mapped_) + entry->e.offset;
            tx_available_.push_front(fbl::move(entry));
        }

        return MX_OK;
    }

    mx_status_t Start() {
        ssize_t rc = ioctl_ethernet_start(fd_);
        return rc < 0 ? static_cast<mx_status_t>(rc) : MX_OK;
    }

    mx_status_t Stop() {
        ssize_t rc = ioctl_ethernet_stop(fd_);
        return rc < 0 ? static_cast<mx_status_t>(rc) : MX_OK;
    }

    mx_status_t GetStatus(uint32_t* eth_status) {
        ssize_t rc = ioctl_ethernet_get_status(fd_, eth_status);
        return rc < 0 ? static_cast<mx_status_t>(rc) : MX_OK;
    }

    mx::fifo* tx_fifo() { return &tx_; }
    mx::fifo* rx_fifo() { return &rx_; }
    uint32_t tx_depth() { return tx_depth_; }
    uint32_t rx_depth() { return rx_depth_; }

    uint8_t* GetRxBuffer(uint32_t offset) {
        return reinterpret_cast<uint8_t*>(mapped_) + offset;
    }

    eth_fifo_entry_t* GetTxBuffer() {
        auto entry_ptr = tx_available_.pop_front();
        eth_fifo_entry_t* entry = nullptr;
        if (entry_ptr != nullptr) {
            entry = &entry_ptr->e;
            tx_pending_.push_front(fbl::move(entry_ptr));
        }
        return entry;
    }

    void ReturnTxBuffer(eth_fifo_entry_t* entry) {
        auto entry_ptr = tx_pending_.erase_if(
                [entry](const FifoEntry& tx_entry) { return tx_entry.e.cookie == entry->cookie; });
        if (entry_ptr != nullptr) {
            tx_available_.push_front(fbl::move(entry_ptr));
        }
    }

  private:
    int fd_;

    uint64_t vmo_size_ = 0;
    mx::vmo buf_;
    uintptr_t mapped_ = 0;
    uint32_t nbufs_ = 0;
    uint16_t bufsize_ = 0;

    mx::fifo tx_;
    mx::fifo rx_;
    uint32_t tx_depth_ = 0;
    uint32_t rx_depth_ = 0;

    using FifoEntryPtr = fbl::unique_ptr<FifoEntry>;
    fbl::SinglyLinkedList<FifoEntryPtr> tx_available_;
    fbl::SinglyLinkedList<FifoEntryPtr> tx_pending_;
};

}  // namespace

static bool EthernetStartTest() {
    // Create the ethertap device
    mx::socket sock;
    ASSERT_EQ(MX_OK, CreateEthertap(1500, &sock));

    // Open the ethernet device
    int devfd = -1;
    ASSERT_EQ(MX_OK, OpenEthertapDev(&devfd));
    ASSERT_GE(devfd, 0);

    // Set up an ethernet client
    EthernetClient client(devfd);
    ASSERT_EQ(MX_OK, client.Register(kTapDevName, 32, 2048));

    // Verify no signals asserted on the rx fifo
    mx_signals_t obs;
    client.rx_fifo()->wait_one(ETH_SIGNAL_STATUS, 0, &obs);
    EXPECT_FALSE(obs & ETH_SIGNAL_STATUS);

    // Start the ethernet client
    EXPECT_EQ(MX_OK, client.Start());

    // Default link status should be OFFLINE
    uint32_t eth_status = 0;
    EXPECT_EQ(MX_OK, client.GetStatus(&eth_status));
    EXPECT_EQ(0, eth_status);

    // Set the link status to online and verify
    sock.signal_peer(0, ETHERTAP_SIGNAL_ONLINE);

    EXPECT_EQ(MX_OK,
            client.rx_fifo()->wait_one(ETH_SIGNAL_STATUS, mx::deadline_after(MX_MSEC(10)), &obs));
    EXPECT_TRUE(obs & ETH_SIGNAL_STATUS);

    EXPECT_EQ(MX_OK, client.GetStatus(&eth_status));
    EXPECT_EQ(ETH_STATUS_ONLINE, eth_status);

    // Shutdown the ethernet client
    EXPECT_EQ(MX_OK, client.Stop());

    // Clean up the ethertap device
    sock.reset();

    return true;
}

static bool EthernetLinkStatusTest() {
    // Create the ethertap device
    mx::socket sock;
    ASSERT_EQ(MX_OK, CreateEthertap(1500, &sock));

    // Set the link status to online
    sock.signal_peer(0, ETHERTAP_SIGNAL_ONLINE);

    // Open the ethernet device
    int devfd = -1;
    ASSERT_EQ(MX_OK, OpenEthertapDev(&devfd));
    ASSERT_GE(devfd, 0);

    // Set up an ethernet client
    EthernetClient client(devfd);
    ASSERT_EQ(MX_OK, client.Register(kTapDevName, 32, 2048));

    // Start the ethernet client
    EXPECT_EQ(MX_OK, client.Start());

    // Link status should be ONLINE since we set it before starting the client
    uint32_t eth_status = 0;
    EXPECT_EQ(MX_OK, client.GetStatus(&eth_status));
    EXPECT_EQ(ETH_STATUS_ONLINE, eth_status);

    // Now the device goes offline
    sock.signal_peer(0, ETHERTAP_SIGNAL_OFFLINE);

    // Verify the link status
    mx_signals_t obs;
    EXPECT_EQ(MX_OK,
            client.rx_fifo()->wait_one(ETH_SIGNAL_STATUS, mx::deadline_after(MX_MSEC(10)), &obs));
    EXPECT_TRUE(obs & ETH_SIGNAL_STATUS);

    EXPECT_EQ(MX_OK, client.GetStatus(&eth_status));
    EXPECT_EQ(0, eth_status);

    // Shutdown the ethernet client
    EXPECT_EQ(MX_OK, client.Stop());

    // Clean up the ethertap device
    sock.reset();

    return true;
}

static bool EthernetDataTest_Send() {
    // Set up the tap device and the ethernet client
    mx::socket sock;
    ASSERT_EQ(MX_OK, CreateEthertap(1500, &sock));

    int devfd = -1;
    ASSERT_EQ(MX_OK, OpenEthertapDev(&devfd));
    ASSERT_GE(devfd, 0);

    EthernetClient client(devfd);
    ASSERT_EQ(MX_OK, client.Register(kTapDevName, 32, 2048));
    ASSERT_EQ(MX_OK, client.Start());

    sock.signal_peer(0, ETHERTAP_SIGNAL_ONLINE);

    // Ensure that the fifo is writable
    mx_signals_t obs;
    EXPECT_EQ(MX_OK, client.tx_fifo()->wait_one(MX_FIFO_WRITABLE, 0, &obs));
    ASSERT_TRUE(obs & MX_FIFO_WRITABLE);

    // Grab an available TX fifo entry
    auto entry = client.GetTxBuffer();
    ASSERT_TRUE(entry != nullptr);

    // Populate some data
    uint8_t* buf = static_cast<uint8_t*>(entry->cookie);
    for (int i = 0; i < 32; i++) {
        buf[i] = static_cast<uint8_t>(i & 0xff);
    }
    entry->length = 32;

    // Write to the TX fifo
    uint32_t actual = 0;
    ASSERT_EQ(MX_OK, client.tx_fifo()->write(entry, sizeof(eth_fifo_entry_t), &actual));
    EXPECT_EQ(1u, actual);

    // The socket should be readable
    EXPECT_EQ(MX_OK, sock.wait_one(MX_SOCKET_READABLE, mx::deadline_after(MX_MSEC(10)), &obs));
    ASSERT_TRUE(obs & MX_SOCKET_READABLE);

    // Read the data from the socket, which should match what was written to the fifo
    uint8_t read_buf[32];
    size_t actual_sz = 0;
    EXPECT_EQ(MX_OK, sock.read(0u, static_cast<void*>(read_buf), 32, &actual_sz));
    ASSERT_EQ(32, actual_sz);
    EXPECT_BYTES_EQ(buf, read_buf, 32, "");

    // Now the TX completion entry should be available to read from the TX fifo
    EXPECT_EQ(MX_OK,
            client.tx_fifo()->wait_one(MX_FIFO_READABLE, mx::deadline_after(MX_MSEC(10)), &obs));
    ASSERT_TRUE(obs & MX_FIFO_READABLE);

    eth_fifo_entry_t return_entry;
    ASSERT_EQ(MX_OK, client.tx_fifo()->read(&return_entry, sizeof(eth_fifo_entry_t), &actual));
    EXPECT_EQ(1u, actual);

    // Check the flags on the returned entry
    EXPECT_TRUE(return_entry.flags & ETH_FIFO_TX_OK);
    return_entry.flags = 0;

    // Verify the bytes from the rest of the entry match what we wrote
    auto expected_entry = reinterpret_cast<uint8_t*>(entry);
    auto actual_entry = reinterpret_cast<uint8_t*>(&return_entry);
    EXPECT_BYTES_EQ(expected_entry, actual_entry, sizeof(eth_fifo_entry_t), "");

    // Return the buffer to our client; the client destructor will make sure no TXs are still
    // pending at the end of te test.
    client.ReturnTxBuffer(&return_entry);

    // Shutdown the client and cleanup the tap device
    EXPECT_EQ(MX_OK, client.Stop());
    sock.reset();

    return true;
}

static bool EthernetDataTest_Recv() {
    // Set up the tap device and the ethernet client
    mx::socket sock;
    ASSERT_EQ(MX_OK, CreateEthertap(1500, &sock));

    int devfd = -1;
    ASSERT_EQ(MX_OK, OpenEthertapDev(&devfd));
    ASSERT_GE(devfd, 0);

    EthernetClient client(devfd);
    ASSERT_EQ(MX_OK, client.Register(kTapDevName, 32, 2048));
    ASSERT_EQ(MX_OK, client.Start());

    sock.signal_peer(0, ETHERTAP_SIGNAL_ONLINE);

    // The socket should be writable
    mx_signals_t obs;
    EXPECT_EQ(MX_OK, sock.wait_one(MX_SOCKET_WRITABLE, 0, &obs));
    ASSERT_TRUE(obs & MX_SOCKET_WRITABLE);

    // Send a buffer through the socket
    uint8_t buf[32];
    for (int i = 0; i < 32; i++) {
        buf[i] = static_cast<uint8_t>(i & 0xff);
    }
    size_t actual = 0;
    EXPECT_EQ(MX_OK, sock.write(0, static_cast<void*>(buf), 32, &actual));
    EXPECT_EQ(32, actual);

    // The fifo should be readable
    EXPECT_EQ(MX_OK,
            client.rx_fifo()->wait_one(MX_FIFO_READABLE, mx::deadline_after(MX_MSEC(10)), &obs));
    ASSERT_TRUE(obs & MX_FIFO_READABLE);

    // Read the RX fifo
    eth_fifo_entry_t entry;
    uint32_t actual_entries = 0;
    EXPECT_EQ(MX_OK, client.rx_fifo()->read(&entry, sizeof(eth_fifo_entry_t), &actual_entries));
    EXPECT_EQ(1, actual_entries);

    // Check the bytes in the VMO compared to what we sent through the socket
    auto return_buf = client.GetRxBuffer(entry.offset);
    EXPECT_BYTES_EQ(buf, return_buf, entry.length, "");

    // RX fifo should be readable, and we can return the buffer to the driver
    EXPECT_EQ(MX_OK, client.rx_fifo()->wait_one(MX_FIFO_WRITABLE, 0, &obs));
    ASSERT_TRUE(obs & MX_FIFO_WRITABLE);

    entry.length = 2048;
    EXPECT_EQ(MX_OK, client.rx_fifo()->write(&entry, sizeof(eth_fifo_entry_t), &actual_entries));
    EXPECT_EQ(1, actual_entries);

    // Shutdown the client and cleanup the tap device
    EXPECT_EQ(MX_OK, client.Stop());
    sock.reset();

    return true;
}

BEGIN_TEST_CASE(EthernetSetupTests)
RUN_TEST_MEDIUM(EthernetStartTest)
RUN_TEST_MEDIUM(EthernetLinkStatusTest)
END_TEST_CASE(EthernetSetupTests)

BEGIN_TEST_CASE(EthernetDataTests)
RUN_TEST_MEDIUM(EthernetDataTest_Send)
RUN_TEST_MEDIUM(EthernetDataTest_Recv)
END_TEST_CASE(EthernetDataTests)

int main(int argc, char* argv[]) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
