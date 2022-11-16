// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.ethernet/cpp/wire.h>
#include <fidl/fuchsia.hardware.ethertap/cpp/wire.h>
#include <fuchsia/hardware/ethernet/c/banjo.h>
#include <inttypes.h>
#include <lib/component/incoming/cpp/service_client.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
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

#include <fbl/algorithm.h>
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
const char kTapctl[] = "/dev/sys/test/tapctl";
const uint8_t kTapMacPrefix[] = {0x12, 0x20};

class EthertapClient {
 public:
  EthertapClient() {
    // Each EthertapClient will have a different MAC address based on a monotonically increasing
    // counter. That allows us to deterministically find each device in devfs (see WatchCb).
    auto seed = instance_counter_.fetch_add(1);
    auto* tail = std::copy_n(kTapMacPrefix, sizeof(kTapMacPrefix), mac_.octets.begin());
    std::copy_n(reinterpret_cast<const uint8_t*>(&seed), sizeof(seed), tail);
  }

  ~EthertapClient() { reset(); }

  zx_status_t CreateWithOptions(uint32_t mtu, const char* name, uint32_t options = 0) {
    tap_device_.reset();

    zx::result tap_control = component::Connect<fuchsia_hardware_ethertap::TapControl>(kTapctl);
    if (tap_control.is_error()) {
      return tap_control.error_value();
    }

    zx::result server = fidl::CreateEndpoints(&tap_device_);
    if (server.is_error()) {
      return server.error_value();
    }
    const size_t name_len =
        std::min<size_t>(strlen(name), fuchsia_hardware_ethertap::wire::kMaxNameLength);
    const fidl::WireResult result = fidl::WireCall(tap_control.value())
                                        ->OpenDevice(fidl::StringView::FromExternal(name, name_len),
                                                     {
                                                         .options = options,
                                                         .mtu = mtu,
                                                         .mac = mac_,
                                                     },
                                                     std::move(server.value()));
    if (!result.ok()) {
      return result.status();
    }
    const fidl::WireResponse response = result.value();
    return response.s;
  }

  zx_status_t SetOnline(bool online) {
    zx_signals_t obs;
    if (tap_device_.channel().wait_one(ZX_CHANNEL_WRITABLE, FAIL_TIMEOUT, &obs) != ZX_OK) {
      return ZX_ERR_TIMED_OUT;
    }
    return fidl::WireCall(tap_device_)->SetOnline(online).status();
  }

  zx_status_t Write(uint8_t* data, size_t len) {
    zx_signals_t obs;
    if (tap_device_.channel().wait_one(ZX_CHANNEL_WRITABLE, FAIL_TIMEOUT, &obs) != ZX_OK) {
      return ZX_ERR_TIMED_OUT;
    }
    return fidl::WireCall(tap_device_)
        ->WriteFrame(fidl::VectorView<uint8_t>::FromExternal(data, len))
        .status();
  }

  void DrainEvents(size_t* reads) {
    class EventHandler : public fidl::WireSyncEventHandler<fuchsia_hardware_ethertap::TapDevice> {
     public:
      size_t reads() const { return reads_; }

     private:
      void OnFrame(fidl::WireEvent<fuchsia_hardware_ethertap::TapDevice::OnFrame>* event) override {
        reads_++;
      }

      void OnReportParams(
          fidl::WireEvent<fuchsia_hardware_ethertap::TapDevice::OnReportParams>* event) override {
        reads_++;
      }

      size_t reads_ = 0;
    };
    EventHandler handler;
    while (true) {
      zx_signals_t obs;
      if (zx_status_t status =
              tap_device_.channel().wait_one(ZX_CHANNEL_READABLE, PROPAGATE_TIME, &obs);
          status != ZX_OK) {
        ASSERT_STATUS(status, ZX_ERR_TIMED_OUT);
        if (reads != nullptr) {
          *reads = handler.reads();
        }
        return;
      }
      ASSERT_OK(handler.HandleOneEvent(tap_device_));
    }
  }

  void ExpectDataRead(const void* data, size_t len, const char* msg) {
    class EventHandler : public fidl::WireSyncEventHandler<fuchsia_hardware_ethertap::TapDevice> {
     public:
      EventHandler(const void* data, size_t len, const char* msg)
          : data_(data), len_(len), msg_(msg) {}

     private:
      void OnFrame(fidl::WireEvent<fuchsia_hardware_ethertap::TapDevice::OnFrame>* event) override {
        ASSERT_EQ(event->data.count(), len_, "%s", msg_);
        ASSERT_BYTES_EQ(static_cast<const uint8_t*>(event->data.data()),
                        static_cast<const uint8_t*>(data_), len_, "%s", msg_);
      }

      void OnReportParams(
          fidl::WireEvent<fuchsia_hardware_ethertap::TapDevice::OnReportParams>* event) override {
        ADD_FAILURE("unexpected event: param=%d value=%d", event->param, event->value);
      }

      const void* data_;
      size_t len_;
      const char* msg_;
    };
    EventHandler handler(data, len, msg);
    ASSERT_NO_FATAL_FAILURE(ASSERT_OK(handler.HandleOneEvent(tap_device_)));
  }

  void ExpectSetParam(uint32_t param, int32_t value, size_t len, uint8_t* data, const char* msg) {
    class EventHandler : public fidl::WireSyncEventHandler<fuchsia_hardware_ethertap::TapDevice> {
     public:
      EventHandler(uint32_t param, int32_t value, size_t len, uint8_t* data, const char* msg)
          : param_(param), value_(value), len_(len), data_(data), msg_(msg) {}

     private:
      void OnFrame(fidl::WireEvent<fuchsia_hardware_ethertap::TapDevice::OnFrame>* event) override {
        ADD_FAILURE("unexpected event");
      }

      void OnReportParams(
          fidl::WireEvent<fuchsia_hardware_ethertap::TapDevice::OnReportParams>* event) override {
        ASSERT_EQ(event->param, param_, "%s", msg_);
        ASSERT_EQ(event->value, value_, "%s", msg_);
        ASSERT_EQ(event->data.count(), len_, "%s", msg_);
        ASSERT_BYTES_EQ(static_cast<const uint8_t*>(event->data.data()),
                        static_cast<const uint8_t*>(data_), len_, "%s", msg_);
      }
      uint32_t param_;
      int32_t value_;
      size_t len_;
      uint8_t* data_;
      const char* msg_;
    };
    EventHandler handler(param, value, len, data, msg);
    ASSERT_NO_FATAL_FAILURE(ASSERT_OK(handler.HandleOneEvent(tap_device_)));
  }

  bool valid() const { return tap_device_.is_valid(); }

  void reset() { tap_device_.reset(); }

  const fuchsia_hardware_ethernet::wire::MacAddress& mac() const { return mac_; }

 private:
  static std::atomic_uint32_t instance_counter_;
  fuchsia_hardware_ethernet::wire::MacAddress mac_;
  fidl::ClientEnd<fuchsia_hardware_ethertap::TapDevice> tap_device_;
};
std::atomic_uint32_t EthertapClient::instance_counter_;

struct WatchCookie {
  fidl::ClientEnd<fuchsia_hardware_ethernet::Device> device;
  fuchsia_hardware_ethernet::wire::MacAddress mac;
};
zx_status_t WatchCb(int dirfd, int event, const char* fn, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  if (std::string_view{fn} == ".") {
    return ZX_OK;
  }

  fdio_cpp::UnownedFdioCaller caller(dirfd);
  zx::result device =
      component::ConnectAt<fuchsia_hardware_ethernet::Device>(caller.directory(), fn);
  if (device.is_error()) {
    return device.error_value();
  }
  // See if this device is our ethertap device
  const fidl::WireResult result = fidl::WireCall(device.value())->GetInfo();
  if (!result.ok()) {
    fprintf(stderr, "could not get ethernet info for %s/%s: %s\n", kEthernetDir, fn,
            result.FormatDescription().c_str());
    // Return ZX_OK to keep watching for devices.
    //
    // Why does this fail sometimes? Are we seeing ethertap devices flap?
    return ZX_OK;
  }
  const fidl::WireResponse response = result.value();
  const fuchsia_hardware_ethernet::wire::Info& info = response.info;

  if (!(info.features & fuchsia_hardware_ethernet::wire::Features::kSynthetic)) {
    // Not a match, keep looking.
    return ZX_OK;
  }

  WatchCookie& watch_request = *reinterpret_cast<WatchCookie*>(cookie);
  if (info.mac.octets != watch_request.mac.octets) {
    // Not a match, keep looking.
    return ZX_OK;
  }

  // Found it!
  watch_request.device = std::move(device.value());
  return ZX_ERR_STOP;
}

zx::result<fidl::ClientEnd<fuchsia_hardware_ethernet::Device>> OpenEthertapDev(
    EthertapClient& tap) {
  fbl::unique_fd ethdir(open(kEthernetDir, O_RDONLY));
  if (!ethdir) {
    fprintf(stderr, "could not open %s: %s\n", kEthernetDir, strerror(errno));
    return zx::error(ZX_ERR_IO);
  }

  WatchCookie cookie = {
      .mac = tap.mac(),
  };
  zx_status_t status = fdio_watch_directory(ethdir.get(), WatchCb, zx_deadline_after(ZX_SEC(30)),
                                            reinterpret_cast<void*>(&cookie));
  if (status == ZX_ERR_STOP) {
    return zx::ok(std::move(cookie.device));
  }
  return zx::error(status);
}

struct FifoEntry : public fbl::SinglyLinkedListable<std::unique_ptr<FifoEntry>> {
  eth_fifo_entry_t e;
};

struct EthernetOpenInfo {
  explicit EthernetOpenInfo(const char* name) : name(name) {}
  // Special setup until we have IGMP: turn off multicast-promisc in init.
  bool multicast = false;
  const char* name;
  bool online = true;
  uint32_t options = 0;
};

class EthernetClient {
 public:
  EthernetClient() = default;
  ~EthernetClient() {
    Stop();
    Cleanup();
  }

  void Cleanup() {
    if (mapped_ > 0) {
      zx::vmar::root_self()->unmap(mapped_, vmo_size_);
    }
    ethernet_device_.reset();
  }

  zx_status_t Register(fidl::ClientEnd<fuchsia_hardware_ethernet::Device> ethernet_device,
                       const char* name, uint32_t nbufs, uint16_t bufsize) {
    ethernet_device_ = std::move(ethernet_device);
    {
      const size_t name_len =
          std::min<size_t>(strlen(name), fuchsia_hardware_ethernet::wire::kMaxClientNameLen);
      const fidl::WireResult result =
          fidl::WireCall(ethernet_device_)
              ->SetClientName(fidl::StringView::FromExternal(name, name_len));
      if (!result.ok()) {
        fprintf(stderr, "could not set client name to %s: %s\n", name,
                result.FormatDescription().c_str());
        return result.status();
      }
      const fidl::WireResponse response = result.value();
      if (zx_status_t status = response.status; status != ZX_OK) {
        fprintf(stderr, "could not set client name to %s: %s\n", name,
                zx_status_get_string(status));
        return status;
      }
    }
    {
      const fidl::WireResult result = fidl::WireCall(ethernet_device_)->GetFifos();
      if (!result.ok()) {
        fprintf(stderr, "could not get fifos: %s\n", result.FormatDescription().c_str());
        return result.status();
      }
      const fidl::WireResponse response = result.value();
      if (zx_status_t status = response.status; status != ZX_OK) {
        fprintf(stderr, "could not get fifos: %s\n", zx_status_get_string(status));
        return status;
      }
      fuchsia_hardware_ethernet::wire::Fifos& fifos = *response.info;
      tx_.get() = std::move(fifos.tx);
      rx_.get() = std::move(fifos.rx);
      tx_depth_ = fifos.tx_depth;
      rx_depth_ = fifos.rx_depth;
    }

    nbufs_ = nbufs;
    bufsize_ = bufsize;

    vmo_size_ = 2 * nbufs_ * bufsize_;
    if (zx_status_t status = zx::vmo::create(vmo_size_, 0, &buf_); status != ZX_OK) {
      fprintf(stderr, "could not create a vmo of size %" PRIu64 ": %s\n", vmo_size_,
              zx_status_get_string(status));
      return status;
    }

    if (zx_status_t status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, buf_,
                                                        0, vmo_size_, &mapped_);
        status != ZX_OK) {
      fprintf(stderr, "failed to map vmo: %s\n", zx_status_get_string(status));
      return status;
    }

    {
      zx::vmo dup;
      if (zx_status_t status = buf_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup); status != ZX_OK) {
        fprintf(stderr, "failed to duplicate vmo: %s\n", zx_status_get_string(status));
        return status;
      }
      const fidl::WireResult result = fidl::WireCall(ethernet_device_)->SetIoBuffer(std::move(dup));
      if (!result.ok()) {
        fprintf(stderr, "could not set eth iobuf: %s\n", result.FormatDescription().c_str());
        return result.status();
      }
      const fidl::WireResponse response = result.value();
      if (zx_status_t status = response.status; status != ZX_OK) {
        fprintf(stderr, "could not set eth iobuf: %s\n", zx_status_get_string(status));
        return status;
      }
    }

    uint32_t idx = 0;
    for (; idx < nbufs; idx++) {
      eth_fifo_entry_t entry = {
          .offset = idx * bufsize_,
          .length = bufsize_,
          .flags = 0,
          .cookie = 0,
      };
      if (zx_status_t status = rx_.write_one(entry); status != ZX_OK) {
        fprintf(stderr, "failed call to write(): %s\n", zx_status_get_string(status));
        return status;
      }
    }

    for (; idx < 2 * nbufs; idx++) {
      auto entry = std::make_unique<FifoEntry>();
      entry->e.offset = idx * bufsize_;
      entry->e.length = bufsize_;
      entry->e.flags = 0;
      entry->e.cookie = mapped_ + entry->e.offset;
      tx_available_.push_front(std::move(entry));
    }

    return ZX_OK;
  }

  zx_status_t Start() {
    const fidl::WireResult result = fidl::WireCall(ethernet_device_)->Start();
    if (!result.ok()) {
      return result.status();
    }
    const fidl::WireResponse response = result.value();
    return response.status;
  }

  zx_status_t Stop() { return fidl::WireCall(ethernet_device_)->Stop().status(); }

  zx::result<fuchsia_hardware_ethernet::wire::DeviceStatus> GetStatus() {
    const fidl::WireResult result = fidl::WireCall(ethernet_device_)->GetStatus();
    if (!result.ok()) {
      return zx::error(result.status());
    }
    const fidl::WireResponse response = result.value();
    return zx::ok(response.device_status);
  }

  zx_status_t SetPromisc(bool on) {
    const fidl::WireResult result = fidl::WireCall(ethernet_device_)->SetPromiscuousMode(on);
    if (!result.ok()) {
      return result.status();
    }
    const fidl::WireResponse response = result.value();
    return response.status;
  }

  zx_status_t SetMulticastPromisc(bool on) {
    const fidl::WireResult result =
        fidl::WireCall(ethernet_device_)->ConfigMulticastSetPromiscuousMode(on);
    if (!result.ok()) {
      return result.status();
    }
    const fidl::WireResponse response = result.value();
    return response.status;
  }

  zx_status_t MulticastAddressAdd(fuchsia_hardware_ethernet::wire::MacAddress mac) {
    const fidl::WireResult result = fidl::WireCall(ethernet_device_)->ConfigMulticastAddMac(mac);
    if (!result.ok()) {
      return result.status();
    }
    const fidl::WireResponse response = result.value();
    return response.status;
  }

  zx_status_t MulticastAddressDel(fuchsia_hardware_ethernet::wire::MacAddress mac) {
    const fidl::WireResult result = fidl::WireCall(ethernet_device_)->ConfigMulticastDeleteMac(mac);
    if (!result.ok()) {
      return result.status();
    }
    const fidl::WireResponse response = result.value();
    return response.status;
  }

  // Delete this along with other "multicast_" related code once we have IGMP.
  // This tells the driver to turn off the on-by-default multicast-promisc.
  zx_status_t MulticastInitForTest() {
    const fidl::WireResult result = fidl::WireCall(ethernet_device_)->ConfigMulticastTestFilter();
    if (!result.ok()) {
      return result.status();
    }
    const fidl::WireResponse response = result.value();
    return response.status;
  }

  fzl::fifo<eth_fifo_entry_t>* tx_fifo() { return &tx_; }
  fzl::fifo<eth_fifo_entry_t>* rx_fifo() { return &rx_; }
  uint32_t tx_depth() const { return tx_depth_; }
  uint32_t rx_depth() const { return rx_depth_; }

  uint8_t* GetRxBuffer(uint32_t offset) const {
    return reinterpret_cast<uint8_t*>(mapped_) + offset;
  }

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
  fidl::ClientEnd<fuchsia_hardware_ethernet::Device> ethernet_device_;

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
// ASSERT_NO_FATAL_FAILURE() when calling them.

static void AddClientHelper(EthertapClient& tap, EthernetClient& client,
                            const EthernetOpenInfo& openInfo) {
  // Open the ethernet device
  zx::result ethernet_device = OpenEthertapDev(tap);
  ASSERT_OK(ethernet_device);

  // Initialize the ethernet client
  ASSERT_EQ(ZX_OK, client.Register(std::move(ethernet_device.value()), openInfo.name, 32, 2048));
  if (openInfo.online) {
    // Start the ethernet client
    ASSERT_EQ(ZX_OK, client.Start());
  }
  if (openInfo.multicast) {
    ASSERT_EQ(ZX_OK, client.MulticastInitForTest());
  }
  if (openInfo.options & fuchsia_hardware_ethertap::wire::kOptReportParam) {
    tap.DrainEvents(nullptr);  // internal driver setup probably has caused some reports
  }
}

static void OpenFirstClientHelper(EthertapClient& tap, EthernetClient& client,
                                  const EthernetOpenInfo& openInfo) {
  // Create the ethertap device
  auto options = openInfo.options | fuchsia_hardware_ethertap::wire::kOptTrace;
  if (openInfo.online) {
    options |= fuchsia_hardware_ethertap::wire::kOptOnline;
  }
  ASSERT_EQ(ZX_OK, tap.CreateWithOptions(1500, openInfo.name, options));
  ASSERT_NO_FATAL_FAILURE(AddClientHelper(tap, client, openInfo));
}

TEST(EthernetSetupTests, EthernetImplStartTest) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info("StartTest");
  info.online = false;
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, client, info));

  // Verify no signals asserted on the rx fifo
  zx_signals_t obs = 0;
  client.rx_fifo()->wait_one(fuchsia_hardware_ethernet::wire::kSignalStatus, zx::time(), &obs);
  EXPECT_FALSE(obs & fuchsia_hardware_ethernet::wire::kSignalStatus);

  // Start the ethernet client
  EXPECT_EQ(ZX_OK, client.Start());

  // Verify that the ethernet driver signaled a status change for the initial state.
  obs = 0;
  EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(fuchsia_hardware_ethernet::wire::kSignalStatus,
                                              FAIL_TIMEOUT, &obs));
  EXPECT_TRUE(obs & fuchsia_hardware_ethernet::wire::kSignalStatus);

  // Default link status should be OFFLINE
  {
    zx::result eth_status = client.GetStatus();
    ASSERT_OK(eth_status);
    EXPECT_EQ(fuchsia_hardware_ethernet::wire::DeviceStatus{}, eth_status.value());
  }

  // Set the link status to online and verify
  EXPECT_EQ(ZX_OK, tap.SetOnline(true));

  EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(fuchsia_hardware_ethernet::wire::kSignalStatus,
                                              FAIL_TIMEOUT, &obs));
  EXPECT_TRUE(obs & fuchsia_hardware_ethernet::wire::kSignalStatus);

  {
    zx::result eth_status = client.GetStatus();
    ASSERT_OK(eth_status);
    EXPECT_EQ(fuchsia_hardware_ethernet::wire::DeviceStatus::kOnline, eth_status.value());
  }
}

TEST(EthernetSetupTests, EthernetLinkStatusTest) {
  // Create the ethertap device
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info("LinkStatus");
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, client, info));

  // Verify that the ethernet driver signaled a status change for the initial state.
  zx_signals_t obs = 0;
  EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(fuchsia_hardware_ethernet::wire::kSignalStatus,
                                              FAIL_TIMEOUT, &obs));
  EXPECT_TRUE(obs & fuchsia_hardware_ethernet::wire::kSignalStatus);

  // Link status should be ONLINE since it's set in OpenFirstClientHelper
  {
    zx::result eth_status = client.GetStatus();
    ASSERT_OK(eth_status);
    EXPECT_EQ(fuchsia_hardware_ethernet::wire::DeviceStatus::kOnline, eth_status.value());
  }

  // Now the device goes offline
  EXPECT_EQ(ZX_OK, tap.SetOnline(false));

  // Verify the link status
  obs = 0;
  EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(fuchsia_hardware_ethernet::wire::kSignalStatus,
                                              FAIL_TIMEOUT, &obs));
  EXPECT_TRUE(obs & fuchsia_hardware_ethernet::wire::kSignalStatus);

  {
    zx::result eth_status = client.GetStatus();
    ASSERT_OK(eth_status);
    EXPECT_EQ(fuchsia_hardware_ethernet::wire::DeviceStatus{}, eth_status.value());
  }
}

TEST(EthernetConfigTests, EthernetSetPromiscMultiClientTest) {
  EthertapClient tap;
  EthernetClient clientA;
  EthernetOpenInfo info("SetPromiscA");
  info.options = fuchsia_hardware_ethertap::wire::kOptReportParam;
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, clientA, info));
  EthernetClient clientB;
  info.name = "SetPromiscB";
  ASSERT_NO_FATAL_FAILURE(AddClientHelper(tap, clientB, info));

  ASSERT_EQ(ZX_OK, clientA.SetPromisc(true));

  ASSERT_NO_FATAL_FAILURE(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 1, 0, nullptr, "Promisc on (1)"));

  // None of these should cause a change in promisc commands to ethermac.
  ASSERT_EQ(ZX_OK, clientA.SetPromisc(true));  // It was already requested by A.
  ASSERT_EQ(ZX_OK, clientB.SetPromisc(true));
  ASSERT_EQ(ZX_OK, clientA.SetPromisc(false));  // A should now not want it, but B still does.
  size_t reads;
  ASSERT_NO_FATAL_FAILURE(tap.DrainEvents(&reads));
  EXPECT_EQ(0, reads);

  // After the next line, no one wants promisc, so I should get a command to turn it off.
  ASSERT_EQ(ZX_OK, clientB.SetPromisc(false));
  ASSERT_NO_FATAL_FAILURE(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 0, 0, nullptr, "Promisc should be off (2)"));
}

TEST(EthernetConfigTests, EthernetSetPromiscClearOnCloseTest) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info("PromiscClear");
  info.options = fuchsia_hardware_ethertap::wire::kOptReportParam;
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, client, info));

  ASSERT_EQ(ZX_OK, client.SetPromisc(true));

  ASSERT_NO_FATAL_FAILURE(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 1, 0, nullptr, "Promisc on (1)"));

  // Shutdown the ethernet client.
  EXPECT_EQ(ZX_OK, client.Stop());
  client.Cleanup();  // This will free devfd

  // That should have caused promisc to turn off.
  ASSERT_NO_FATAL_FAILURE(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 0, 0, nullptr, "Promisc should be off (2)"));

  // Clean up the ethertap device.
  tap.reset();
}

TEST(EthernetConfigTests, EthernetMulticastRejectsUnicastAddress) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info("RejectUni");
  info.options = fuchsia_hardware_ethertap::wire::kOptReportParam;
  info.multicast = true;
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, client, info));

  fuchsia_hardware_ethernet::wire::MacAddress unicastMac = {
      .octets = {2, 4, 6, 8, 10, 12},  // For multicast, LSb of MSB should be 1
  };
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, client.MulticastAddressAdd(unicastMac));
}

TEST(EthernetConfigTests, EthernetMulticastSetsAddresses) {
  EthertapClient tap;
  EthernetClient clientA;
  EthernetOpenInfo info("MultiAdrTestA");
  info.options = fuchsia_hardware_ethertap::wire::kOptReportParam;
  info.multicast = true;
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, clientA, info));
  info.name = "MultiAdrTestB";
  EthernetClient clientB;
  ASSERT_NO_FATAL_FAILURE(AddClientHelper(tap, clientB, info));

  fuchsia_hardware_ethernet::wire::MacAddress macA = {
      .octets = {1, 2, 3, 4, 5, 6},
  };
  fuchsia_hardware_ethernet::wire::MacAddress macB = {
      .octets = {7, 8, 9, 10, 11, 12},
  };
  fuchsia_hardware_ethernet::wire::MacAddress data = {
      .octets = {6, 12},
  };
  ASSERT_EQ(ZX_OK, clientA.MulticastAddressAdd(macA));

  ASSERT_NO_FATAL_FAILURE(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, 1, 1,
                                             data.octets.data(), "first addr"));
  ASSERT_EQ(ZX_OK, clientB.MulticastAddressAdd(macB));
  ASSERT_NO_FATAL_FAILURE(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, 2, 2,
                                             data.octets.data(), "second addr"));
}

// This value is implementation dependent, set in
// src/connectivity/ethernet/drivers/ethernet/ethernet.c
enum { MULTICAST_LIST_LIMIT = 32 };

TEST(EthernetConfigTests, EthernetMulticastPromiscOnOverflow) {
  EthertapClient tap;
  EthernetClient clientA;
  EthernetOpenInfo info("McPromOvA");
  info.options = fuchsia_hardware_ethertap::wire::kOptReportParam;
  info.multicast = true;
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, clientA, info));
  EthernetClient clientB;
  info.name = "McPromOvB";
  ASSERT_NO_FATAL_FAILURE(AddClientHelper(tap, clientB, info));
  fuchsia_hardware_ethernet::wire::MacAddress mac = {
      .octets = {1, 2, 3, 4, 5, 0},
  };
  uint8_t& last_octet = mac.octets[5];
  uint8_t data[MULTICAST_LIST_LIMIT];
  ASSERT_LT(MULTICAST_LIST_LIMIT, 255);  // If false, add code to avoid duplicate mac addresses
  uint8_t next_val = 0x11;  // Any value works; starting at 0x11 makes the dump extra readable.
  uint32_t n_data = 0;
  for (uint32_t i = 0; i < MULTICAST_LIST_LIMIT - 1; i++) {
    last_octet = next_val;
    data[n_data++] = next_val++;
    ASSERT_EQ(ZX_OK, clientA.MulticastAddressAdd(mac));
    ASSERT_NO_FATAL_FAILURE(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, n_data, n_data,
                                               data, "loading filter"));
  }
  ASSERT_EQ(n_data, MULTICAST_LIST_LIMIT - 1);  // There should be 1 space left
  last_octet = next_val;
  data[n_data++] = next_val++;
  ASSERT_EQ(ZX_OK, clientB.MulticastAddressAdd(mac));
  ASSERT_NO_FATAL_FAILURE(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, n_data, n_data,
                                             data, "b - filter should be full"));
  last_octet = next_val++;
  ASSERT_EQ(ZX_OK, clientB.MulticastAddressAdd(mac));
  ASSERT_NO_FATAL_FAILURE(
      tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, -1, 0, nullptr, "overloaded B"));
  // Drop a client, multicast filtering for it must be dropped as well.
  clientB.Cleanup();
  n_data--;
  ASSERT_NO_FATAL_FAILURE(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, n_data, n_data,
                                             data, "deleted B - filter should have 31"));
  last_octet = next_val;
  data[n_data++] = next_val++;
  ASSERT_EQ(ZX_OK, clientA.MulticastAddressAdd(mac));
  ASSERT_NO_FATAL_FAILURE(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, n_data, n_data,
                                             data, "a - filter should be full"));
  last_octet = next_val++;
  ASSERT_EQ(ZX_OK, clientA.MulticastAddressAdd(mac));
  ASSERT_NO_FATAL_FAILURE(
      tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER, -1, 0, nullptr, "overloaded A"));
}

TEST(EthernetConfigTests, EthernetSetMulticastPromiscMultiClientTest) {
  EthertapClient tap;
  EthernetClient clientA;
  EthernetOpenInfo info("MultiPromiscA");
  info.options = fuchsia_hardware_ethertap::wire::kOptReportParam;
  info.multicast = true;
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, clientA, info));
  EthernetClient clientB;
  info.name = "MultiPromiscB";
  ASSERT_NO_FATAL_FAILURE(AddClientHelper(tap, clientB, info));

  clientA.SetMulticastPromisc(true);
  ASSERT_NO_FATAL_FAILURE(
      tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, 0, nullptr, "Promisc on (1)"));

  // None of these should cause a change in promisc commands to ethermac.
  clientA.SetMulticastPromisc(true);  // It was already requested by A.
  clientB.SetMulticastPromisc(true);
  clientA.SetMulticastPromisc(false);  // A should now not want it, but B still does.
  size_t reads;
  ASSERT_NO_FATAL_FAILURE(tap.DrainEvents(&reads));
  EXPECT_EQ(0, reads);

  // After the next line, no one wants promisc, so I should get a command to turn it off.
  clientB.SetMulticastPromisc(false);
  // That should have caused promisc to turn off.
  ASSERT_NO_FATAL_FAILURE(tap.ExpectSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 0, 0, nullptr,
                                             "Closed: promisc off (2)"));
}

TEST(EthernetConfigTests, EthernetSetMulticastPromiscClearOnCloseTest) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info("MCPromiscClear");
  info.options = fuchsia_hardware_ethertap::wire::kOptReportParam;
  info.multicast = true;
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, client, info));

  ASSERT_EQ(ZX_OK, client.SetPromisc(true));

  ASSERT_NO_FATAL_FAILURE(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 1, 0, nullptr, "Promisc on (1)"));

  // Shutdown the ethernet client.
  EXPECT_EQ(ZX_OK, client.Stop());
  client.Cleanup();  // This will free devfd

  // That should have caused promisc to turn off.
  ASSERT_NO_FATAL_FAILURE(
      tap.ExpectSetParam(ETHERNET_SETPARAM_PROMISC, 0, 0, nullptr, "Closed: promisc off (2)"));

  // Clean up the ethertap device.
  tap.reset();
}

TEST(EthernetDataTests, EthernetDataTest_Send) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info("DataSend");
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, client, info));

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
}

TEST(EthernetDataTests, EthernetDataTest_Recv) {
  EthertapClient tap;
  EthernetClient client;
  EthernetOpenInfo info("DataRecv");
  ASSERT_NO_FATAL_FAILURE(OpenFirstClientHelper(tap, client, info));

  // Send a buffer through the tap channel
  uint8_t buf[32];
  for (int i = 0; i < 32; i++) {
    buf[i] = static_cast<uint8_t>(i & 0xff);
  }
  EXPECT_EQ(ZX_OK, tap.Write(buf, sizeof(buf)));

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
}

int main(int argc, char** argv) {
  auto args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
  args.sys_device_driver = "/boot/driver/test-parent-sys.so";

  // NB: this loop is never run. RealmBuilder::Build is in the call stack, and insists on a non-null
  // dispatcher.
  //
  // TODO(https://fxbug.dev/114254): Remove this.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  zx::result devmgr =
      devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), loop.dispatcher());
  if (devmgr.is_error()) {
    fprintf(stderr, "Could not create driver manager, %d\n", devmgr.status_value());
    return devmgr.status_value();
  }

  fdio_ns_t* ns;
  if (zx_status_t status = fdio_ns_get_installed(&ns); status != ZX_OK) {
    fprintf(stderr, "Could not create get namespace, %d\n", status);
    return status;
  }
  if (zx_status_t status = fdio_ns_bind_fd(ns, "/dev", devmgr.value().devfs_root().get());
      status != ZX_OK) {
    fprintf(stderr, "Could not bind /dev namespace, %d\n", status);
    return status;
  }

  fbl::unique_fd ctl;
  if (zx_status_t status = device_watcher::RecursiveWaitForFile(devmgr.value().devfs_root(),
                                                                "sys/test/tapctl", &ctl);
      status != ZX_OK) {
    fprintf(stderr, "sys/test/tapctl failed to enumerate: %d\n", status);
    return status;
  }

  return RUN_ALL_TESTS(argc, argv);
}
