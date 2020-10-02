// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/ethernet/c/fidl.h>
#include <fuchsia/hardware/ethertap/c/fidl.h>
#include <inttypes.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fzl/fifo.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/device/ethernet.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <utility>

#include <ddk/protocol/ethernet.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

// Delay for data to work through the system. The test will pause this long, so it's best
// to keep it fairly short. If it's too short, the test will occasionally be flaky,
// especially on qemu.
static constexpr zx::duration kPropagateDuration = zx::msec(200);
#define PROPAGATE_TIME (zx::deadline_after(kPropagateDuration))

// We expect something to happen prior to timeout, and the test will fail if it doesn't. So
// wait longer to further reduce the likelihood of test flakiness.
#define FAIL_TIMEOUT (zx::deadline_after(kPropagateDuration * 50))

namespace {

const char kEthernetDir[] = "/dev/class/ethernet";
const char kTapctl[] = "/dev/test/tapctl";
const uint8_t kTapMacPrefix[] = {0x12, 0x20};

const char* mxstrerror(zx_status_t status) { return zx_status_get_string(status); }

class EthertapClient {
 public:
  EthertapClient() {
    // Each EthertapClient will have a different MAC address based on a monotonically increasing
    // counter. That allows us to deterministically find each device in devfs (see WatchCb).
    auto seed = instance_counter_.fetch_add(1);
    auto* tail = std::copy_n(kTapMacPrefix, sizeof(kTapMacPrefix), mac_.begin());
    std::copy_n(reinterpret_cast<const uint8_t*>(&seed), sizeof(seed), tail);
  }

  zx_status_t CreateWithOptions(uint32_t mtu, const char* name, uint32_t options = 0) {
    channel_.reset();

    zx::channel tap_control, tap_control_remote;
    auto status = zx::channel::create(0, &tap_control, &tap_control_remote);
    if (status != ZX_OK) {
      return status;
    }
    status = fdio_service_connect(kTapctl, tap_control_remote.release());
    if (status != ZX_OK) {
      return status;
    }

    fuchsia_hardware_ethertap_Config config;
    config.mtu = mtu;
    config.options = options;
    config.features = 0;
    std::copy(mac_.begin(), mac_.end(), config.mac.octets);

    zx::channel remote;
    status = zx::channel::create(0, &channel_, &remote);
    if (status != ZX_OK) {
      return status;
    }

    zx_status_t o_status;
    status = fuchsia_hardware_ethertap_TapControlOpenDevice(tap_control.get(), name, strlen(name),
                                                            &config, remote.get(), &o_status);
    if (status != ZX_OK) {
      channel_.reset();
      return status;
    } else if (o_status != ZX_OK) {
      channel_.reset();
      return o_status;
    }
    return ZX_OK;
  }

  zx_status_t SetOnline(bool online) {
    zx_signals_t obs;
    if (channel_.wait_one(ZX_CHANNEL_WRITABLE, FAIL_TIMEOUT, &obs) != ZX_OK) {
      return ZX_ERR_TIMED_OUT;
    }
    return fuchsia_hardware_ethertap_TapDeviceSetOnline(channel_.get(), online);
  }

  zx_status_t Write(const void* data, size_t len) {
    zx_signals_t obs;
    if (channel_.wait_one(ZX_CHANNEL_WRITABLE, FAIL_TIMEOUT, &obs) != ZX_OK) {
      return ZX_ERR_TIMED_OUT;
    }
    return fuchsia_hardware_ethertap_TapDeviceWriteFrame(channel_.get(),
                                                         static_cast<const uint8_t*>(data), len);
  }

  void DrainEvents(int* reads) {
    constexpr int READBUF_SIZE = fuchsia_hardware_ethertap_MAX_MTU * 2;
    zx_signals_t obs;
    uint8_t read_buf[READBUF_SIZE];
    uint32_t actual_sz = 0;
    uint32_t actual_handles = 0;
    zx_status_t status = ZX_OK;
    *reads = 0;

    while (ZX_OK == (status = channel_.wait_one(ZX_CHANNEL_READABLE, PROPAGATE_TIME, &obs))) {
      status = channel_.read(0u, static_cast<void*>(read_buf), nullptr, READBUF_SIZE, 0, &actual_sz,
                             &actual_handles);
      ASSERT_EQ(ZX_OK, status);
      auto* msg = reinterpret_cast<fidl_message_header_t*>(read_buf);
      switch (msg->ordinal) {
        case fuchsia_hardware_ethertap_TapDeviceOnFrameOrdinal:
        case fuchsia_hardware_ethertap_TapDeviceOnReportParamsOrdinal:
          (*reads)++;
          break;
        default:
          break;
      }
    }
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT);
  }

  template <typename T>
  void ExpectEvent(uint64_t ordinal, const fidl_type_t* table, std::function<void(T* data)> check,
                   const char* msg) {
    constexpr int READBUF_SIZE = fuchsia_hardware_ethertap_MAX_MTU * 2;
    zx_signals_t obs;
    uint8_t read_buf[READBUF_SIZE];
    // The channel should be readable
    ASSERT_EQ(ZX_OK, channel_.wait_one(ZX_CHANNEL_READABLE, FAIL_TIMEOUT, &obs), "%s", msg);
    ASSERT_TRUE(obs & ZX_CHANNEL_READABLE, "%s", msg);

    fidl::Message message(fidl::BytePart(read_buf, READBUF_SIZE), fidl::HandlePart());
    ASSERT_EQ(ZX_OK, message.Read(channel_.get(), 0), "%s", msg);
    ASSERT_EQ(message.ordinal(), ordinal, "%s", msg);
    const char* fidl_err = nullptr;
    ASSERT_EQ(ZX_OK, message.Decode(table, &fidl_err), "%s", fidl_err);
    auto* frame = message.GetBytesAs<T>();

    check(frame);
  }

  void ExpectDataRead(const void* data, size_t len, const char* msg) {
    ASSERT_NO_FATAL_FAILURES(ExpectEvent<fuchsia_hardware_ethertap_TapDeviceOnFrameEvent>(
        fuchsia_hardware_ethertap_TapDeviceOnFrameOrdinal,
        &fuchsia_hardware_ethertap_TapDeviceOnFrameEventTable,
        [data, len, msg](fuchsia_hardware_ethertap_TapDeviceOnFrameEvent* frame) {
          ASSERT_EQ(frame->data.count, len, "%s", msg);
          if (len > 0) {
            ASSERT_BYTES_EQ(static_cast<const uint8_t*>(frame->data.data),
                            static_cast<const uint8_t*>(data), len, "%s", msg);
          }
        },
        msg));
  }

  void ExpectSetParam(uint32_t param, int32_t value, size_t len, uint8_t* data, const char* msg) {
    ASSERT_NO_FATAL_FAILURES(ExpectEvent<fuchsia_hardware_ethertap_TapDeviceOnReportParamsEvent>(
        fuchsia_hardware_ethertap_TapDeviceOnReportParamsOrdinal,
        &fuchsia_hardware_ethertap_TapDeviceOnReportParamsEventTable,
        [param, value, data, len,
         msg](fuchsia_hardware_ethertap_TapDeviceOnReportParamsEvent* report) {
          ASSERT_EQ(report->param, param, "%s", msg);
          ASSERT_EQ(report->value, value, "%s", msg);
          ASSERT_EQ(report->data.count, len, "%s", msg);
          if (len > 0) {
            ASSERT_BYTES_EQ(static_cast<const uint8_t*>(report->data.data),
                            static_cast<const uint8_t*>(data), len, "%s", msg);
          }
        },
        msg));
  }

  bool valid() const { return channel_.is_valid(); }

  void reset() { channel_.reset(); }

  const std::array<uint8_t, ETH_MAC_SIZE>& mac() const { return mac_; }

 private:
  static std::atomic_uint32_t instance_counter_;
  std::array<uint8_t, ETH_MAC_SIZE> mac_;
  zx::channel channel_;
};
std::atomic_uint32_t EthertapClient::instance_counter_;

struct WatchCookie {
  zx::channel device;
  std::array<uint8_t, ETH_MAC_SIZE> mac_search;
};
zx_status_t WatchCb(int dirfd, int event, const char* fn, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  if (!strcmp(fn, ".") || !strcmp(fn, "..")) {
    return ZX_OK;
  }

  zx::channel svc;
  {
    int devfd = openat(dirfd, fn, O_RDONLY);
    if (devfd < 0) {
      return ZX_OK;
    }

    zx_status_t status = fdio_get_service_handle(devfd, svc.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }
  }

  // See if this device is our ethertap device
  fuchsia_hardware_ethernet_Info info;
  zx_status_t status = fuchsia_hardware_ethernet_DeviceGetInfo(svc.get(), &info);
  if (status != ZX_OK) {
    fprintf(stderr, "could not get ethernet info for %s/%s: %s\n", kEthernetDir, fn,
            mxstrerror(status));
    // Return ZX_OK to keep watching for devices.
    return ZX_OK;
  }
  if (!(info.features & fuchsia_hardware_ethernet_Features_SYNTHETIC)) {
    // Not a match, keep looking.
    return ZX_OK;
  }

  auto* watch_request = reinterpret_cast<WatchCookie*>(cookie);
  if (memcmp(info.mac.octets, watch_request->mac_search.data(), ETH_MAC_SIZE) != 0) {
    // Not a match, keep looking.
    return ZX_OK;
  }

  // Found it!
  watch_request->device = std::move(svc);
  return ZX_ERR_STOP;
}

zx_status_t OpenEthertapDev(zx::channel* svc, EthertapClient* tap) {
  if (svc == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  int ethdir = open(kEthernetDir, O_RDONLY);
  if (ethdir < 0) {
    fprintf(stderr, "could not open %s: %s\n", kEthernetDir, strerror(errno));
    return ZX_ERR_IO;
  }

  WatchCookie cookie;
  cookie.mac_search = tap->mac();
  zx_status_t status;
  status = fdio_watch_directory(ethdir, WatchCb, zx_deadline_after(ZX_SEC(2)),
                                reinterpret_cast<void*>(&cookie));
  if (status == ZX_ERR_STOP) {
    *svc = std::move(cookie.device);
    return ZX_OK;
  } else {
    return status;
  }
}

struct FifoEntry : public fbl::SinglyLinkedListable<std::unique_ptr<FifoEntry>> {
  eth_fifo_entry_t e;
};

struct EthernetOpenInfo {
  EthernetOpenInfo(const char* name) : name(name) {}
  // Special setup until we have IGMP: turn off multicast-promisc in init.
  bool multicast = false;
  const char* name;
  bool online = true;
  uint32_t options = 0;
};

class EthernetClient {
 public:
  EthernetClient() {}
  ~EthernetClient() { Cleanup(); }

  void Cleanup() {
    if (mapped_ > 0) {
      zx::vmar::root_self()->unmap(mapped_, vmo_size_);
    }
    svc_.reset();
  }

  zx_status_t Register(zx::channel svc, const char* name, uint32_t nbufs, uint16_t bufsize) {
    svc_ = std::move(svc);
    zx_status_t call_status = ZX_OK;
    size_t name_len = std::min<size_t>(strlen(name), fuchsia_hardware_ethernet_MAX_CLIENT_NAME_LEN);
    zx_status_t status =
        fuchsia_hardware_ethernet_DeviceSetClientName(svc_.get(), name, name_len, &call_status);
    if (status != ZX_OK || call_status != ZX_OK) {
      fprintf(stderr, "could not set client name to %s: %d, %d\n", name, status, call_status);
      return status == ZX_OK ? call_status : status;
    }

    fuchsia_hardware_ethernet_Fifos fifos;
    status = fuchsia_hardware_ethernet_DeviceGetFifos(svc_.get(), &call_status, &fifos);
    if (status != ZX_OK || call_status != ZX_OK) {
      fprintf(stderr, "could not get fifos: %d, %d\n", status, call_status);
      return status == ZX_OK ? call_status : status;
    }

    tx_.reset(fifos.tx);
    rx_.reset(fifos.rx);
    tx_depth_ = fifos.tx_depth;
    rx_depth_ = fifos.rx_depth;

    nbufs_ = nbufs;
    bufsize_ = bufsize;

    vmo_size_ = 2 * nbufs_ * bufsize_;
    status = zx::vmo::create(vmo_size_, 0, &buf_);
    if (status != ZX_OK) {
      fprintf(stderr, "could not create a vmo of size %" PRIu64 ": %s\n", vmo_size_,
              mxstrerror(status));
      return status;
    }

    status = zx::vmar::root_self()->map(0, buf_, 0, vmo_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                        &mapped_);
    if (status != ZX_OK) {
      fprintf(stderr, "failed to map vmo: %s\n", mxstrerror(status));
      return status;
    }

    zx::vmo buf_copy;
    status = buf_.duplicate(ZX_RIGHT_SAME_RIGHTS, &buf_copy);
    if (status != ZX_OK) {
      fprintf(stderr, "failed to duplicate vmo: %s\n", mxstrerror(status));
      return status;
    }

    zx_handle_t bufh = buf_copy.release();
    status = fuchsia_hardware_ethernet_DeviceSetIOBuffer(svc_.get(), bufh, &call_status);
    if (status != ZX_OK || call_status != ZX_OK) {
      fprintf(stderr, "failed to set eth iobuf: %d, %d\n", status, call_status);
      return status == ZX_OK ? call_status : status;
    }

    uint32_t idx = 0;
    for (; idx < nbufs; idx++) {
      eth_fifo_entry_t entry = {
          .offset = idx * bufsize_,
          .length = bufsize_,
          .flags = 0,
          .cookie = 0,
      };
      status = rx_.write_one(entry);
      if (status != ZX_OK) {
        fprintf(stderr, "failed call to write(): %s\n", mxstrerror(status));
        return status;
      }
    }

    for (; idx < 2 * nbufs; idx++) {
      auto entry = std::unique_ptr<FifoEntry>(new FifoEntry);
      entry->e.offset = idx * bufsize_;
      entry->e.length = bufsize_;
      entry->e.flags = 0;
      entry->e.cookie = mapped_ + entry->e.offset;
      tx_available_.push_front(std::move(entry));
    }

    return ZX_OK;
  }

  zx_status_t Start() {
    zx_status_t call_status = ZX_OK;
    zx_status_t status = fuchsia_hardware_ethernet_DeviceStart(svc_.get(), &call_status);
    if (status != ZX_OK) {
      return status;
    }
    return call_status;
  }

  zx_status_t Stop() { return fuchsia_hardware_ethernet_DeviceStop(svc_.get()); }

  zx_status_t GetStatus(uint32_t* eth_status) {
    return fuchsia_hardware_ethernet_DeviceGetStatus(svc_.get(), eth_status);
  }

  zx_status_t SetPromisc(bool on) {
    zx_status_t call_status = ZX_OK;
    zx_status_t status =
        fuchsia_hardware_ethernet_DeviceSetPromiscuousMode(svc_.get(), on, &call_status);
    if (status != ZX_OK) {
      return status;
    }
    return call_status;
  }

  zx_status_t SetMulticastPromisc(bool on) {
    zx_status_t call_status, status;
    status = fuchsia_hardware_ethernet_DeviceConfigMulticastSetPromiscuousMode(svc_.get(), on,
                                                                               &call_status);
    if (status != ZX_OK) {
      return status;
    }
    return call_status;
  }

  zx_status_t MulticastAddressAdd(uint8_t* mac_addr) {
    fuchsia_hardware_ethernet_MacAddress mac;
    memcpy(mac.octets, mac_addr, 6);

    zx_status_t call_status, status;
    status = fuchsia_hardware_ethernet_DeviceConfigMulticastAddMac(svc_.get(), &mac, &call_status);
    if (status != ZX_OK) {
      return status;
    }
    return call_status;
  }

  zx_status_t MulticastAddressDel(uint8_t* mac_addr) {
    fuchsia_hardware_ethernet_MacAddress mac;
    memcpy(mac.octets, mac_addr, 6);

    zx_status_t call_status, status;
    status =
        fuchsia_hardware_ethernet_DeviceConfigMulticastDeleteMac(svc_.get(), &mac, &call_status);
    if (status != ZX_OK) {
      return status;
    }
    return call_status;
  }

  // Delete this along with other "multicast_" related code once we have IGMP.
  // This tells the driver to turn off the on-by-default multicast-promisc.
  zx_status_t MulticastInitForTest() {
    zx_status_t call_status, status;
    status = fuchsia_hardware_ethernet_DeviceConfigMulticastTestFilter(svc_.get(), &call_status);
    if (status != ZX_OK) {
      return status;
    }
    return call_status;
  }

  fzl::fifo<eth_fifo_entry_t>* tx_fifo() { return &tx_; }
  fzl::fifo<eth_fifo_entry_t>* rx_fifo() { return &rx_; }
  uint32_t tx_depth() { return tx_depth_; }
  uint32_t rx_depth() { return rx_depth_; }

  uint8_t* GetRxBuffer(uint32_t offset) { return reinterpret_cast<uint8_t*>(mapped_) + offset; }

  eth_fifo_entry_t* GetTxBuffer() {
    auto entry_ptr = tx_available_.pop_front();
    eth_fifo_entry_t* entry = nullptr;
    if (entry_ptr != nullptr) {
      entry = &entry_ptr->e;
      tx_pending_.push_front(std::move(entry_ptr));
    }
    return entry;
  }

  void ReturnTxBuffer(eth_fifo_entry_t* entry) {
    auto entry_ptr = tx_pending_.erase_if(
        [entry](const FifoEntry& tx_entry) { return tx_entry.e.cookie == entry->cookie; });
    if (entry_ptr != nullptr) {
      tx_available_.push_front(std::move(entry_ptr));
    }
  }

 private:
  zx::channel svc_;

  uint64_t vmo_size_ = 0;
  zx::vmo buf_;
  uintptr_t mapped_ = 0;
  uint32_t nbufs_ = 0;
  uint16_t bufsize_ = 0;

  fzl::fifo<eth_fifo_entry_t> tx_;
  fzl::fifo<eth_fifo_entry_t> rx_;
  uint32_t tx_depth_ = 0;
  uint32_t rx_depth_ = 0;

  using FifoEntryPtr = std::unique_ptr<FifoEntry>;
  fbl::SinglyLinkedList<FifoEntryPtr> tx_available_;
  fbl::SinglyLinkedList<FifoEntryPtr> tx_pending_;
};

}  // namespace

// Functions named ...Helper are intended to be called from every test function for
// setup and teardown of the ethdevs.
// To generate informative error messages in case they fail, use
// ASSERT_NO_FATAL_FAILURES() when calling them.

static void AddClientHelper(EthertapClient* tap, EthernetClient* client,
                            const EthernetOpenInfo& openInfo) {
  // Open the ethernet device
  zx::channel svc;
  ASSERT_EQ(ZX_OK, OpenEthertapDev(&svc, tap));
  ASSERT_TRUE(svc.is_valid());

  // Initialize the ethernet client
  ASSERT_EQ(ZX_OK, client->Register(std::move(svc), openInfo.name, 32, 2048));
  if (openInfo.online) {
    // Start the ethernet client
    ASSERT_EQ(ZX_OK, client->Start());
  }
  if (openInfo.multicast) {
    ASSERT_EQ(ZX_OK, client->MulticastInitForTest());
  }
  if (openInfo.options & fuchsia_hardware_ethertap_OPT_REPORT_PARAM) {
    int reads;
    tap->DrainEvents(&reads);  // internal driver setup probably has caused some reports
  }
}

static void OpenFirstClientHelper(EthertapClient* tap, EthernetClient* client,
                                  const EthernetOpenInfo& openInfo) {
  // Create the ethertap device
  auto options = openInfo.options | fuchsia_hardware_ethertap_OPT_TRACE;
  if (openInfo.online) {
    options |= fuchsia_hardware_ethertap_OPT_ONLINE;
  }
  char name[fuchsia_hardware_ethertap_MAX_NAME_LENGTH + 1];
  strncpy(name, openInfo.name, fuchsia_hardware_ethertap_MAX_NAME_LENGTH);
  name[fuchsia_hardware_ethertap_MAX_NAME_LENGTH] = '\0';
  ASSERT_EQ(ZX_OK, tap->CreateWithOptions(1500, name, options));
  ASSERT_NO_FATAL_FAILURES(AddClientHelper(tap, client, openInfo));
}

static void EthernetCleanupHelper(EthertapClient* tap, EthernetClient* client,
                                  EthernetClient* client2 = nullptr) {
  // Note: Don't keep adding client params; find another way if more than 2 clients.

  // Shutdown the ethernet client(s)
  ASSERT_EQ(ZX_OK, client->Stop());
  if (client2 != nullptr) {
    ASSERT_EQ(ZX_OK, client->Stop());
  }

  // Clean up the ethertap device
  tap->reset();
}

TEST(EthernetSetupTests, EthernetImplStartTest) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info(__func__);
  info.online = false;
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &client, info));

  // Verify no signals asserted on the rx fifo
  zx_signals_t obs = 0;
  client.rx_fifo()->wait_one(fuchsia_hardware_ethernet_SIGNAL_STATUS, zx::time(), &obs);
  EXPECT_FALSE(obs & fuchsia_hardware_ethernet_SIGNAL_STATUS);

  // Start the ethernet client
  EXPECT_EQ(ZX_OK, client.Start());

  // Verify that the ethernet driver signaled a status change for the initial state.
  obs = 0;
  EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(fuchsia_hardware_ethernet_SIGNAL_STATUS, FAIL_TIMEOUT,
                                              &obs));
  EXPECT_TRUE(obs & fuchsia_hardware_ethernet_SIGNAL_STATUS);

  // Default link status should be OFFLINE
  uint32_t eth_status = 0;
  EXPECT_EQ(ZX_OK, client.GetStatus(&eth_status));
  EXPECT_EQ(0, eth_status);

  // Set the link status to online and verify
  EXPECT_EQ(ZX_OK, tap.SetOnline(true));

  EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(fuchsia_hardware_ethernet_SIGNAL_STATUS, FAIL_TIMEOUT,
                                              &obs));
  EXPECT_TRUE(obs & fuchsia_hardware_ethernet_SIGNAL_STATUS);

  EXPECT_EQ(ZX_OK, client.GetStatus(&eth_status));
  EXPECT_EQ(fuchsia_hardware_ethernet_DeviceStatus_ONLINE, eth_status);

  ASSERT_NO_FATAL_FAILURES(EthernetCleanupHelper(&tap, &client));
}

TEST(EthernetSetupTests, EthernetLinkStatusTest) {
  // Create the ethertap device
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info(__func__);
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &client, info));

  // Verify that the ethernet driver signaled a status change for the initial state.
  zx_signals_t obs = 0;
  EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(fuchsia_hardware_ethernet_SIGNAL_STATUS, FAIL_TIMEOUT,
                                              &obs));
  EXPECT_TRUE(obs & fuchsia_hardware_ethernet_SIGNAL_STATUS);

  // Link status should be ONLINE since it's set in OpenFirstClientHelper
  uint32_t eth_status = 0;
  EXPECT_EQ(ZX_OK, client.GetStatus(&eth_status));
  EXPECT_EQ(fuchsia_hardware_ethernet_DeviceStatus_ONLINE, eth_status);

  // Now the device goes offline
  EXPECT_EQ(ZX_OK, tap.SetOnline(false));

  // Verify the link status
  obs = 0;
  EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(fuchsia_hardware_ethernet_SIGNAL_STATUS, FAIL_TIMEOUT,
                                              &obs));
  EXPECT_TRUE(obs & fuchsia_hardware_ethernet_SIGNAL_STATUS);

  EXPECT_EQ(ZX_OK, client.GetStatus(&eth_status));
  EXPECT_EQ(0, eth_status);

  ASSERT_NO_FATAL_FAILURES(EthernetCleanupHelper(&tap, &client));
}

TEST(EthernetConfigTests, EthernetSetPromiscMultiClientTest) {
  EthertapClient tap;
  EthernetClient clientA;
  EthernetOpenInfo info("SetPromiscA");
  info.options = fuchsia_hardware_ethertap_OPT_REPORT_PARAM;
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &clientA, info));
  EthernetClient clientB;
  info.name = "SetPromiscB";
  ASSERT_NO_FATAL_FAILURES(AddClientHelper(&tap, &clientB, info));

  ASSERT_EQ(ZX_OK, clientA.SetPromisc(true));

  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 1, 0, nullptr, "Promisc on (1)"));

  // None of these should cause a change in promisc commands to ethermac.
  ASSERT_EQ(ZX_OK, clientA.SetPromisc(true));  // It was already requested by A.
  ASSERT_EQ(ZX_OK, clientB.SetPromisc(true));
  ASSERT_EQ(ZX_OK, clientA.SetPromisc(false));  // A should now not want it, but B still does.
  int reads;
  ASSERT_NO_FATAL_FAILURES(tap.DrainEvents(&reads));
  EXPECT_EQ(0, reads);

  // After the next line, no one wants promisc, so I should get a command to turn it off.
  ASSERT_EQ(ZX_OK, clientB.SetPromisc(false));
  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 0, 0, nullptr, "Promisc should be off (2)"));

  ASSERT_NO_FATAL_FAILURES(EthernetCleanupHelper(&tap, &clientA, &clientB));
}

TEST(EthernetConfigTests, EthernetSetPromiscClearOnCloseTest) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info(__func__);
  info.options = fuchsia_hardware_ethertap_OPT_REPORT_PARAM;
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &client, info));

  ASSERT_EQ(ZX_OK, client.SetPromisc(true));

  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 1, 0, nullptr, "Promisc on (1)"));

  // Shutdown the ethernet client.
  EXPECT_EQ(ZX_OK, client.Stop());
  client.Cleanup();  // This will free devfd

  // That should have caused promisc to turn off.
  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 0, 0, nullptr, "Promisc should be off (2)"));

  // Clean up the ethertap device.
  tap.reset();
}

TEST(EthernetConfigTests, EthernetMulticastRejectsUnicastAddress) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info(__func__);
  info.options = fuchsia_hardware_ethertap_OPT_REPORT_PARAM;
  info.multicast = true;
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &client, info));

  uint8_t unicastMac[] = {2, 4, 6, 8, 10, 12};  // For multicast, LSb of MSB should be 1
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, client.MulticastAddressAdd(unicastMac));

  ASSERT_NO_FATAL_FAILURES(EthernetCleanupHelper(&tap, &client));
}

TEST(EthernetConfigTests, EthernetMulticastSetsAddresses) {
  EthertapClient tap;
  EthernetClient clientA;
  EthernetOpenInfo info("MultiAdrTestA");
  info.options = fuchsia_hardware_ethertap_OPT_REPORT_PARAM;
  info.multicast = true;
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &clientA, info));
  info.name = "MultiAdrTestB";
  EthernetClient clientB;
  ASSERT_NO_FATAL_FAILURES(AddClientHelper(&tap, &clientB, info));

  uint8_t macA[] = {1, 2, 3, 4, 5, 6};
  uint8_t macB[] = {7, 8, 9, 10, 11, 12};
  uint8_t data[] = {6, 12};
  ASSERT_EQ(ZX_OK, clientA.MulticastAddressAdd(macA));

  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, 1, 1, data, "first addr"));
  ASSERT_EQ(ZX_OK, clientB.MulticastAddressAdd(macB));
  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, 2, 2, data, "second addr"));
  ASSERT_NO_FATAL_FAILURES(EthernetCleanupHelper(&tap, &clientA, &clientB));
}

// This value is implementation dependent, set in
// src/connectivity/ethernet/drivers/ethernet/ethernet.c
#define MULTICAST_LIST_LIMIT 32

TEST(EthernetConfigTests, EthernetMulticastPromiscOnOverflow) {
  EthertapClient tap;
  EthernetClient clientA;
  EthernetOpenInfo info("McPromOvA");
  info.options = fuchsia_hardware_ethertap_OPT_REPORT_PARAM;
  info.multicast = true;
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &clientA, info));
  EthernetClient clientB;
  info.name = "McPromOvB";
  ASSERT_NO_FATAL_FAILURES(AddClientHelper(&tap, &clientB, info));
  uint8_t mac[] = {1, 2, 3, 4, 5, 0};
  uint8_t data[MULTICAST_LIST_LIMIT];
  ASSERT_LT(MULTICAST_LIST_LIMIT, 255);  // If false, add code to avoid duplicate mac addresses
  uint8_t next_val = 0x11;  // Any value works; starting at 0x11 makes the dump extra readable.
  uint32_t n_data = 0;
  for (uint32_t i = 0; i < MULTICAST_LIST_LIMIT - 1; i++) {
    mac[5] = next_val;
    data[n_data++] = next_val++;
    ASSERT_EQ(ZX_OK, clientA.MulticastAddressAdd(mac));
    ASSERT_NO_FATAL_FAILURES(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, n_data, n_data,
                                                data, "loading filter"));
  }
  ASSERT_EQ(n_data, MULTICAST_LIST_LIMIT - 1);  // There should be 1 space left
  mac[5] = next_val;
  data[n_data++] = next_val++;
  ASSERT_EQ(ZX_OK, clientB.MulticastAddressAdd(mac));
  ASSERT_NO_FATAL_FAILURES(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, n_data, n_data,
                                              data, "b - filter should be full"));
  mac[5] = next_val++;
  ASSERT_EQ(ZX_OK, clientB.MulticastAddressAdd(mac));
  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, -1, 0, nullptr, "overloaded B"));
  // Drop a client, multicast filtering for it must be dropped as well.
  clientB.Cleanup();
  n_data--;
  ASSERT_NO_FATAL_FAILURES(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, n_data, n_data,
                                              data, "deleted B - filter should have 31"));
  mac[5] = next_val;
  data[n_data++] = next_val++;
  ASSERT_EQ(ZX_OK, clientA.MulticastAddressAdd(mac));
  ASSERT_NO_FATAL_FAILURES(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, n_data, n_data,
                                              data, "a - filter should be full"));
  mac[5] = next_val++;
  ASSERT_EQ(ZX_OK, clientA.MulticastAddressAdd(mac));
  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, -1, 0, nullptr, "overloaded A"));
  ASSERT_NO_FATAL_FAILURES(EthernetCleanupHelper(&tap, &clientA));
}

TEST(EthernetConfigTests, EthernetSetMulticastPromiscMultiClientTest) {
  EthertapClient tap;
  EthernetClient clientA;
  EthernetOpenInfo info("MultiPromiscA");
  info.options = fuchsia_hardware_ethertap_OPT_REPORT_PARAM;
  info.multicast = true;
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &clientA, info));
  EthernetClient clientB;
  info.name = "MultiPromiscB";
  ASSERT_NO_FATAL_FAILURES(AddClientHelper(&tap, &clientB, info));

  clientA.SetMulticastPromisc(true);
  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, 0, nullptr, "Promisc on (1)"));

  // None of these should cause a change in promisc commands to ethermac.
  clientA.SetMulticastPromisc(true);  // It was already requested by A.
  clientB.SetMulticastPromisc(true);
  clientA.SetMulticastPromisc(false);  // A should now not want it, but B still does.
  int reads;
  ASSERT_NO_FATAL_FAILURES(tap.DrainEvents(&reads));
  EXPECT_EQ(0, reads);

  // After the next line, no one wants promisc, so I should get a command to turn it off.
  clientB.SetMulticastPromisc(false);
  // That should have caused promisc to turn off.
  ASSERT_NO_FATAL_FAILURES(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 0, 0, nullptr,
                                              "Closed: promisc off (2)"));

  ASSERT_NO_FATAL_FAILURES(EthernetCleanupHelper(&tap, &clientA, &clientB));
}

TEST(EthernetConfigTests, EthernetSetMulticastPromiscClearOnCloseTest) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info(__func__);
  info.options = fuchsia_hardware_ethertap_OPT_REPORT_PARAM;
  info.multicast = true;
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &client, info));

  ASSERT_EQ(ZX_OK, client.SetPromisc(true));

  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 1, 0, nullptr, "Promisc on (1)"));

  // Shutdown the ethernet client.
  EXPECT_EQ(ZX_OK, client.Stop());
  client.Cleanup();  // This will free devfd

  // That should have caused promisc to turn off.
  ASSERT_NO_FATAL_FAILURES(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 0, 0, nullptr, "Closed: promisc off (2)"));

  // Clean up the ethertap device.
  tap.reset();
}

TEST(EthernetDataTests, EthernetDataTest_Send) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info(__func__);
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &client, info));

  // Ensure that the fifo is writable
  zx_signals_t obs;
  EXPECT_EQ(ZX_OK, client.tx_fifo()->wait_one(ZX_FIFO_WRITABLE, zx::time(), &obs));
  ASSERT_TRUE(obs & ZX_FIFO_WRITABLE);

  // Grab an available TX fifo entry
  auto entry = client.GetTxBuffer();
  ASSERT_TRUE(entry != nullptr);

  // Populate some data
  uint8_t* buf = reinterpret_cast<uint8_t*>(entry->cookie);
  for (int i = 0; i < 32; i++) {
    buf[i] = static_cast<uint8_t>(i & 0xff);
  }
  entry->length = 32;

  // Write to the TX fifo
  ASSERT_EQ(ZX_OK, client.tx_fifo()->write_one(*entry));

  tap.ExpectDataRead(buf, 32, "");

  // Now the TX completion entry should be available to read from the TX fifo
  EXPECT_EQ(ZX_OK, client.tx_fifo()->wait_one(ZX_FIFO_READABLE, FAIL_TIMEOUT, &obs));
  ASSERT_TRUE(obs & ZX_FIFO_READABLE);

  eth_fifo_entry_t return_entry;
  ASSERT_EQ(ZX_OK, client.tx_fifo()->read_one(&return_entry));

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

  ASSERT_NO_FATAL_FAILURES(EthernetCleanupHelper(&tap, &client));
}

TEST(EthernetDataTests, EthernetDataTest_Recv) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info(__func__);
  ASSERT_NO_FATAL_FAILURES(OpenFirstClientHelper(&tap, &client, info));

  // Send a buffer through the tap channel
  uint8_t buf[32];
  for (int i = 0; i < 32; i++) {
    buf[i] = static_cast<uint8_t>(i & 0xff);
  }
  EXPECT_EQ(ZX_OK, tap.Write(static_cast<void*>(buf), 32));

  zx_signals_t obs;
  // The fifo should be readable
  EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(ZX_FIFO_READABLE, FAIL_TIMEOUT, &obs));
  ASSERT_TRUE(obs & ZX_FIFO_READABLE);

  // Read the RX fifo
  eth_fifo_entry_t entry;
  EXPECT_EQ(ZX_OK, client.rx_fifo()->read_one(&entry));

  // Check the bytes in the VMO compared to what we sent through the tap channel
  auto return_buf = client.GetRxBuffer(entry.offset);
  EXPECT_BYTES_EQ(buf, return_buf, entry.length, "");

  // RX fifo should be readable, and we can return the buffer to the driver
  EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(ZX_FIFO_WRITABLE, zx::time(), &obs));
  ASSERT_TRUE(obs & ZX_FIFO_WRITABLE);

  entry.length = 2048;
  EXPECT_EQ(ZX_OK, client.rx_fifo()->write_one(entry));

  ASSERT_NO_FATAL_FAILURES(EthernetCleanupHelper(&tap, &client));
}

int main(int argc, char** argv) {
  auto args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
  args.driver_search_paths.push_back("/boot/driver");
  args.load_drivers.push_back("/boot/driver/ethertap.so");
  args.path_prefix = "/pkg/";

  devmgr_integration_test::IsolatedDevmgr devmgr;
  zx_status_t status = devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), &devmgr);
  if (status != ZX_OK) {
    fprintf(stderr, "Could not create driver manager, %d\n", status);
    return status;
  }

  fdio_ns_t* ns;
  status = fdio_ns_get_installed(&ns);
  if (status != ZX_OK) {
    fprintf(stderr, "Could not create get namespace, %d\n", status);
    return status;
  }
  status = fdio_ns_bind_fd(ns, "/dev", devmgr.devfs_root().get());
  if (status != ZX_OK) {
    fprintf(stderr, "Could not bind /dev namespace, %d\n", status);
    return status;
  }

  fbl::unique_fd ctl;
  status = devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/tapctl", &ctl);
  if (status != ZX_OK) {
    fprintf(stderr, "test/tapctl failed to enumerate: %d\n", status);
    return status;
  }

  return RUN_ALL_TESTS(argc, argv);
}
