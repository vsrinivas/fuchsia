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
#include <mxtl/auto_call.h>
#include <mxtl/type_support.h>
#include <mxtl/unique_ptr.h>
#include <mxtl/vector.h>
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
    auto closer = mxtl::MakeAutoCall([ctlfd]() { close(ctlfd); });

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
    auto closer = mxtl::MakeAutoCall([devfd]() { close(devfd); });

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

class EthernetClient {
  public:
    explicit EthernetClient(int fd) : fd_(fd) {}
    ~EthernetClient() {
        mx::vmar::root_self().unmap(mapped_, nbufs_ * bufsize_);
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

        mx_status_t status = mx::vmo::create(2 * nbufs_ * bufsize_, 0u, &buf_);
        if (status != MX_OK) {
            return status;
        }

        status = mx::vmar::root_self().map(0, buf_, 0, nbufs_ * bufsize_,
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

        if (!tx_available_.reserve(nbufs) || !tx_pending_.reserve(nbufs)) {
            return MX_ERR_NO_MEMORY;
        }
        for (; idx < 2 * nbufs; idx++) {
            auto entry = mxtl::unique_ptr<eth_fifo_entry_t>(new eth_fifo_entry_t);
            entry->offset = idx * bufsize_;
            entry->length = bufsize_;
            entry->flags = 0;
            entry->cookie = nullptr;
            bool __UNUSED r = tx_available_.push_back(mxtl::move(entry));
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

    using EntryPtr = mxtl::unique_ptr<eth_fifo_entry_t>;
    mxtl::Vector<EntryPtr>* AvailableTxBuffers() { return &tx_available_; }
    mxtl::Vector<EntryPtr>* PendingTxBuffers() { return &tx_pending_; }

    mx::fifo* tx_fifo() { return &tx_; }
    mx::fifo* rx_fifo() { return &rx_; }
    uint32_t tx_depth() { return tx_depth_; }
    uint32_t rx_depth() { return rx_depth_; }

  private:
    int fd_;

    mx::vmo buf_;
    uintptr_t mapped_;
    uint32_t nbufs_;
    uint16_t bufsize_;

    mx::fifo tx_;
    mx::fifo rx_;
    uint32_t tx_depth_;
    uint32_t rx_depth_;

    mxtl::Vector<EntryPtr> tx_available_;
    mxtl::Vector<EntryPtr> tx_pending_;
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

BEGIN_TEST_CASE(EthernetSetupTests)
RUN_TEST_MEDIUM(EthernetStartTest)
RUN_TEST_MEDIUM(EthernetLinkStatusTest)
END_TEST_CASE(EthernetSetupTests)

int main(int argc, char* argv[]) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
