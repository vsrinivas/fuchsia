// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fastboot.h"

#include <lib/efi/testing/fake_disk_io_protocol.h>

#include <algorithm>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <efi/system-table.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diskio_test.h"
#include "tcp_test.h"
#include "util.h"
#include "xefi.h"

namespace gigaboot {
namespace {

using ::testing::Contains;
using ::testing::Return;

constexpr uint16_t kTestUdpPort = 1234;
constexpr ip6_addr kTestIpAddress = {0x01, 0x23, 0x45, 0x67};

// Defined by fastboot documentation.
enum UdpPacketType {
  kUdpPacketTypeError = 0x00,
  kUdpPacketTypeQuery = 0x01,
  kUdpPacketTypeInit = 0x02,
  kUdpPacketTypeFastboot = 0x03
};

// Extracts a network-order uint16 from the given data.
uint16_t ExtractU16(const uint8_t* data) { return static_cast<uint16_t>(data[0] << 8) | data[1]; }

// Extracts a uint16 MSB and LSB to reduce boilerplate.
uint8_t U16Msb(uint16_t value) { return value >> 8; }
uint8_t U16Lsb(uint16_t value) { return value & 0xFF; }

// Returns true if |data| starts with |prefix|.
bool StartsWith(const std::vector<uint8_t>& data, const std::vector<uint8_t>& prefix) {
  return data.size() >= prefix.size() && memcmp(data.data(), prefix.data(), prefix.size()) == 0;
}

// Returns a 8-byte network-order TCP packet size as a string.
std::string TcpSizeString(uint64_t size) {
  const uint64_t network_order_size = htonll(size);
  std::string ret(sizeof(network_order_size), '\0');
  memcpy(&ret[0], &network_order_size, sizeof(network_order_size));
  return ret;
}

// Returns a fastboot TCP packet as a string.
// TCP packets consist of 8 bytes length + contents.
std::string TcpPacket(std::string_view contents) {
  return TcpSizeString(contents.length()) + std::string(contents);
}

// Stub SetVariable function to allow fastboot tests to set boot action.
EFIAPI efi_status StubSetVariable(char16_t* name, efi_guid* guid, uint32_t flags, size_t length,
                                  const void* data) {
  return EFI_SUCCESS;
}

// Stub SetVariable function to allow fastboot tests to set boot action.
EFIAPI efi_status StubGetVariable(char16_t* name, efi_guid* guid, uint32_t* flags, size_t* length,
                                  void* data) {
  return EFI_SUCCESS;
}

// Helper to fake fastboot UDP input and output.
//
// On creation, injects custom UDP functions into fastboot so we can run the
// loop while controlling what gets sent. On deletion, returns fastboot to
// normal functionality.
//
// Only one of these object can live at a time.
class FakeFastbootUdp {
 public:
  FakeFastbootUdp() {
    if (instance_) {
      fprintf(stderr, "ERROR: cannot create multiple FakeFastbootUdp objects - exiting\n");
      exit(1);
    }
    instance_ = this;
    fb_set_udp_functions_for_testing(RawPollFunc, RawSendFunc);
  }

  ~FakeFastbootUdp() {
    instance_ = nullptr;
    fb_set_udp_functions_for_testing(nullptr, nullptr);
  }

  // Queues a packet to be sent into fastboot on the next loop.
  void QueuePacket(std::vector<uint8_t> data) { tx_packets_.push(std::move(data)); }

  // Pops the earliest packet received from fastboot, or an empty packet if none
  // exist.
  std::vector<uint8_t> ReceivePacket() {
    if (rx_packets_.empty()) {
      return {};
    }
    auto ret = std::move(rx_packets_.front());
    rx_packets_.pop();
    return ret;
  }

  // Sends and receives the 4 packets necessary to initialize a fastboot UDP
  // session: TX query -> RX query response -> TX init -> RX init response.
  //
  // Registers test failure on error.
  void InitializeSession() {
    fb_bootimg_t bootimg;

    // Send a query packet to get the current sequence number.
    const auto query_packet = std::vector<uint8_t>{kUdpPacketTypeQuery, 0, 0, 0};
    QueuePacket(query_packet);
    ASSERT_EQ(POLL, fb_poll(&bootimg));

    // Response packet should be the same query with 2 additional bytes giving
    // the current sequence number.
    auto response = ReceivePacket();
    ASSERT_EQ(response.size(), 6u);
    ASSERT_TRUE(StartsWith(response, query_packet));
    uint16_t sequence = ExtractU16(&response[4]);

    // Send an init packet: version = 0x0001, max packet size = 0x0800.
    QueuePacket(
        {kUdpPacketTypeInit, 0x00, U16Msb(sequence), U16Lsb(sequence), 0x00, 0x01, 0x08, 0x00});
    ASSERT_EQ(POLL, fb_poll(&bootimg));

    response = ReceivePacket();
    ASSERT_EQ(response.size(), 8u);
    ASSERT_TRUE(
        StartsWith(response, {kUdpPacketTypeInit, 0x00, U16Msb(sequence), U16Lsb(sequence)}));
    // UDP protocol version should be 1.
    ASSERT_EQ(ExtractU16(&response[4]), 1);
    // Should support at least 512-byte packets or downloads will take forever.
    ASSERT_GE(ExtractU16(&response[6]), 512);
  }

  // Returns true if fastboot is currently in UDP mode, false otherwise.
  bool InUdpMode() {
    // Reset the flag and poll once, if it gets set back to true we're in UDP.
    poll_called_ = false;
    fb_bootimg_t bootimg;
    fb_poll(&bootimg);
    return poll_called_;
  }

 private:
  // Raw C functions to bounce into our active instance.
  static void RawPollFunc() { instance_->PollFunc(); }
  static int RawSendFunc(const void* data, size_t len, const ip6_addr* daddr, uint16_t dport,
                         uint16_t sport) {
    return instance_->SendFunc(data, len, daddr, dport, sport);
  }

  // Sends the next packet.
  void PollFunc() {
    poll_called_ = true;
    if (!tx_packets_.empty()) {
      std::vector<uint8_t>& packet = tx_packets_.front();
      fb_recv(packet.data(), packet.size(), &kTestIpAddress, kTestUdpPort);
      tx_packets_.pop();
    }
  }

  int SendFunc(const void* data, size_t len, const ip6_addr* daddr, uint16_t dport,
               uint16_t sport) {
    EXPECT_EQ(memcmp(daddr, &kTestIpAddress, sizeof(kTestIpAddress)), 0);
    EXPECT_EQ(dport, kTestUdpPort);
    EXPECT_EQ(sport, FB_SERVER_PORT);

    const uint8_t* data_bytes = reinterpret_cast<const uint8_t*>(data);
    rx_packets_.emplace(data_bytes, data_bytes + len);
    return 0;
  }

  // Global instance to bounce raw C functions into our object.
  static FakeFastbootUdp* instance_;

  std::queue<std::vector<uint8_t>> tx_packets_;
  std::queue<std::vector<uint8_t>> rx_packets_;

  bool poll_called_;
};

FakeFastbootUdp* FakeFastbootUdp::instance_ = nullptr;

// Helper to fake fastboot TCP input and output.
//
// On creation, injects custom TCP functions into fastboot so we can run the
// loop while controlling what gets sent. On deletion, returns fastboot to
// normal functionality.
class FakeFastbootTcp {
 public:
  FakeFastbootTcp() {
    // Fastboot uses the global xefi pointers pretty heavily, replace them with
    // our mocks.
    gBS = system_table_.BootServices = mock_tcp_.boot_services().services();
    system_table_.RuntimeServices = &mock_runtime_services;
    gSys = &system_table_;
    gImg = ImageHandle();

    // Mock out send/receive to inject and receive test data by default.
    //
    // This is a bit tricky due to the 2-part EFI API; Receive()/Transmit() are
    // only called once to initiate the transfer, and CheckEvent() is used from
    // then on to poll the status.
    //
    // To simulate this properly, we need to inject data in the CheckEvent()
    // call rather than Receive()/Transmit(), so that we can do things like mock
    // data arriving after a few CheckEvent() loops have passed.
    ON_CALL(mock_tcp_.client_protocol(), Receive).WillByDefault([this](efi_tcp6_io_token* token) {
      receive_token_ = token;
      return EFI_SUCCESS;
    });
    ON_CALL(mock_tcp_.client_protocol(), Transmit).WillByDefault([this](efi_tcp6_io_token* token) {
      transmit_token_ = token;
      return EFI_SUCCESS;
    });

    ON_CALL(mock_tcp_.boot_services(), CheckEvent)
        .WillByDefault([this](efi_event event) -> efi_status {
          if (receive_token_ && receive_token_->CompletionToken.Event == event) {
            // Fastboot is polling on Receive().
            // Disconnect if requested, otherwise inject any queued test data.
            if (disconnect_) {
              receive_token_->CompletionToken.Status = EFI_CONNECTION_FIN;
              return EFI_SUCCESS;
            }

            if (tx_data_.empty()) {
              return EFI_NOT_READY;
            }

            size_t size =
                std::min(tx_data_.size(), size_t{receive_token_->Packet.RxData->DataLength});
            memcpy(receive_token_->Packet.RxData->FragmentTable[0].FragmentBuffer, tx_data_.data(),
                   size);
            receive_token_->Packet.RxData->DataLength = static_cast<uint32_t>(size);
            receive_token_->Packet.RxData->FragmentTable[0].FragmentLength =
                static_cast<uint32_t>(size);
            tx_data_.erase(tx_data_.begin(), tx_data_.begin() + size);
            receive_token_->CompletionToken.Status = EFI_SUCCESS;
            return EFI_SUCCESS;
          }

          if (transmit_token_ && transmit_token_->CompletionToken.Event == event) {
            // Fastboot is polling on Transmit().
            // Disconnect if requested, otherwise save the data it's sending.
            if (disconnect_) {
              transmit_token_->CompletionToken.Status = EFI_CONNECTION_FIN;
              return EFI_SUCCESS;
            }

            const char* data = reinterpret_cast<const char*>(
                transmit_token_->Packet.TxData->FragmentTable[0].FragmentBuffer);
            rx_data_.insert(rx_data_.end(), data,
                            data + transmit_token_->Packet.TxData->DataLength);
            transmit_token_->CompletionToken.Status = EFI_SUCCESS;
            return EFI_SUCCESS;
          }

          return EFI_SUCCESS;
        });
  }

  ~FakeFastbootTcp() {
    gBS = nullptr;
    gSys = nullptr;
    gImg = nullptr;
    fb_reset_tcp_state_for_testing();

    // Make sure we used all the expected TCP data.
    EXPECT_TRUE(tx_data_.empty());
    EXPECT_TRUE(rx_data_.empty());
  }

  efi::MockBootServices& boot_services() { return mock_tcp_.boot_services(); }
  efi::MockTcp6Protocol& server_protocol() { return mock_tcp_.server_protocol(); }

  // Injects TCP data into fastboot and runs the loop until it gets pushed
  // through.
  //
  // Returns true if the data was read by fastboot, false if it's still pending
  // after running the loop a bunch of times (which may indicate that the
  // fastboot loop got into an unexpected state).
  bool SendData(std::string_view data) {
    tx_data_.insert(tx_data_.end(), data.begin(), data.end());

    int poll_count = 0;
    while (!tx_data_.empty()) {
      fb_bootimg_t bootimg;
      fb_poll(&bootimg);
      if (++poll_count >= 100) {
        return false;
      }
    }

    return true;
  }

  // Runs the fastboot loop until some TCP data has been received out, and
  // returns the resulting data.
  //
  // Returns empty string instead if nothing has been received after a large
  // number of iterations, or if the fb_poll() return value changes.
  //
  // Args:
  //   poll_action: if we return early due to non-POLL result and this is
  //                non-null, it will be filled with the result.
  std::string ReceiveData(fb_poll_next_action* poll_action = nullptr) {
    int poll_count = 0;
    while (rx_data_.empty()) {
      fb_bootimg_t bootimg;
      fb_poll_next_action result = fb_poll(&bootimg);
      if (++poll_count >= 100 || result != POLL) {
        if (poll_action) {
          *poll_action = result;
        }
        return "";
      }
    }

    auto ret = std::move(rx_data_);
    rx_data_.clear();
    return ret;
  }

  // Sets mock state such that the next fastboot TCP read/write call will
  // show that the client has disconnected, then run the loop for a bit to
  // flush the state through.
  //
  // Returns the final fb_poll() return value.
  fb_poll_next_action ClientDisconnect() {
    // Make sure we have sent all the data we expected to.
    EXPECT_TRUE(tx_data_.empty());
    disconnect_ = true;
    fb_poll_next_action action = POLL;
    EXPECT_EQ("", ReceiveData(&action));
    return action;
  }

  void InitializeSession() {
    disconnect_ = false;

    // Run the initial fastboot loop to initialize TCP, fastboot won't switch
    // into TCP mode until the stack is up.
    fb_bootimg_t bootimg;
    ASSERT_EQ(POLL, fb_poll(&bootimg));

    // Now we can tell fastboot to switch to TCP.
    fb_tcp_recv();

    ASSERT_TRUE(SendData("FB01"));
    ASSERT_EQ(ReceiveData(), "FB01");
  }

 private:
  MockTcp mock_tcp_;
  efi_system_table system_table_ = {};
  efi_runtime_services mock_runtime_services = efi_runtime_services{
      .GetVariable = StubGetVariable,
      .SetVariable = StubSetVariable,
  };

  // TX/RX data from the perspective of the fastboot client.
  std::string tx_data_;
  std::string rx_data_;

  // Tokens for current in-progress fastboot server receive/transmit calls.
  efi_tcp6_io_token* receive_token_ = nullptr;
  efi_tcp6_io_token* transmit_token_ = nullptr;

  // Set true to mock a fastboot client disconnect.
  bool disconnect_ = false;
};

struct GetvarState {
  std::unique_ptr<DiskFindBootState> boot_disk_state;
  std::unique_ptr<efi::FakeDiskIoProtocol> fake_disk;
};

// Mocks out the necessary EFI commands so that all the getvar functions
// succeed. Without this, trying to run certain getvars will segfault when
// the try to call into a NULL protocol table.
//
// The return value holds the necessary fake disk state and must be kept alive
// until getvar completes.
GetvarState SetupGetvarCommands(efi::MockBootServices& boot_services) {
  GetvarState state{.fake_disk = std::make_unique<efi::FakeDiskIoProtocol>()};

  // For max-download-size we need to mock out GetMemoryMap.
  EXPECT_CALL(boot_services, GetMemoryMap)
      .WillOnce([](size_t* memory_map_size, efi_memory_descriptor*, size_t*, size_t*, uint32_t*) {
        *memory_map_size = 0;
        return EFI_SUCCESS;
      });

  // For A/B/R slot variables we need to fake a "misc" partition at least big
  // enough to hold A/B/R metadata. Contents don't matter, libabr will reset the
  // metadata to default state if it doesn't look valid.
  state.boot_disk_state = SetupBootDisk(boot_services, state.fake_disk->protocol());
  EXPECT_NO_FATAL_FAILURE(SetupDiskPartitions(*state.fake_disk, {kMiscGptEntry}));

  return state;
}

TEST(Fastboot, UdpInitializeSession) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;

  ASSERT_NO_FATAL_FAILURE(udp.InitializeSession());
}

TEST(Fastboot, UdpInUdpMode) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;

  EXPECT_TRUE(udp.InUdpMode());
}

TEST(Fastboot, TcpInitializeSession) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;

  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());
}

TEST(Fastboot, TcpGetvar) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());

  ASSERT_TRUE(tcp.SendData(TcpPacket("getvar:product")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAYgigaboot"));
}

TEST(Fastboot, TcpGetvarAll) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());

  auto getvar_state = SetupGetvarCommands(tcp.boot_services());
  ASSERT_TRUE(tcp.SendData(TcpPacket("getvar:all")));

  // Read until we get OKAY. Each intermediate packet should be an INFO
  // packet that looks like "INFO<name>:<value>".
  std::string reply;
  std::set<std::string> replies;
  do {
    reply = tcp.ReceiveData();
    ASSERT_NE(reply, "");
    replies.insert(reply);
  } while (reply != TcpPacket("OKAY"));

  // Check a few variables that we expect.
  EXPECT_THAT(replies, Contains(TcpPacket("INFOproduct:gigaboot")));
  EXPECT_THAT(replies, Contains(TcpPacket("INFOcurrent-slot:a")));
  EXPECT_THAT(replies, Contains(TcpPacket("INFOslot-successful:a:no")));
  EXPECT_THAT(replies, Contains(TcpPacket("INFOslot-successful:b:no")));
}

TEST(Fastboot, TcpDownload) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());

  ASSERT_TRUE(tcp.SendData(TcpPacket("download:00000006")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("DATA00000006"));
  ASSERT_TRUE(tcp.SendData(TcpPacket("123456")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAY"));
}

TEST(Fastboot, TcpDownloadInParts) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());

  // Data phase is allowed to break a command up into multiple packets.
  ASSERT_TRUE(tcp.SendData(TcpPacket("download:00000006")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("DATA00000006"));
  ASSERT_TRUE(tcp.SendData(TcpPacket("123")));
  ASSERT_EQ(tcp.ReceiveData(), "");
  ASSERT_TRUE(tcp.SendData(TcpPacket("456")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAY"));
}

TEST(Fastboot, TcpMultiCommandSession) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());

  ASSERT_TRUE(tcp.SendData(TcpPacket("getvar:product")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAYgigaboot"));
  ASSERT_TRUE(tcp.SendData(TcpPacket("getvar:version")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAY0.4"));
}

// Disconnect immediately after the initialization handshake.
TEST(Fastboot, TcpSessionDisconnectAfterHandshake) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;

  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());
  EXPECT_EQ(tcp.ClientDisconnect(), POLL);

  // We should be back in UDP mode after disconnecting.
  EXPECT_TRUE(udp.InUdpMode());
}

// Disconnect mid-packet after sending the length but no data.
TEST(Fastboot, TcpSessionDisconnectAfterLength) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());

  ASSERT_TRUE(tcp.SendData(TcpSizeString(4)));
  EXPECT_EQ(tcp.ClientDisconnect(), POLL);

  EXPECT_TRUE(udp.InUdpMode());
}

// Disconnect after sending a packet but before getting the reply.
TEST(Fastboot, TcpSessionDisconnectAfterReadPacket) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());

  ASSERT_TRUE(tcp.SendData(TcpPacket("getvar:product")));
  EXPECT_EQ(tcp.ClientDisconnect(), POLL);

  EXPECT_TRUE(udp.InUdpMode());
}

// Disconnect after a full command completes.
TEST(Fastboot, TcpSessionDisconnectAfterCommand) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());

  ASSERT_TRUE(tcp.SendData(TcpPacket("getvar:product")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAYgigaboot"));
  EXPECT_EQ(tcp.ClientDisconnect(), POLL);

  EXPECT_TRUE(udp.InUdpMode());
}

// Make sure we can repeatedly go into TCP mode.
TEST(Fastboot, TcpMultipleSessions) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;

  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());
  ASSERT_TRUE(tcp.SendData(TcpPacket("getvar:product")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAYgigaboot"));
  EXPECT_EQ(tcp.ClientDisconnect(), POLL);
  EXPECT_TRUE(udp.InUdpMode());

  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());
  ASSERT_TRUE(tcp.SendData(TcpPacket("getvar:version")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAY0.4"));
  EXPECT_EQ(tcp.ClientDisconnect(), POLL);
  EXPECT_TRUE(udp.InUdpMode());
}

// Make sure we re-try opening TCP if it fails the first time due to the
// link not being up yet.
TEST(Fastboot, TcpRetryOpen) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;

  EXPECT_CALL(tcp.server_protocol(), Configure)
      .WillOnce(Return(EFI_INVALID_PARAMETER))  // Fail attempt #1.
      .WillOnce(Return(EFI_SUCCESS));           // Succeed attempt #2.

  fb_bootimg_t bootimg;
  ASSERT_EQ(POLL, fb_poll(&bootimg));
  ASSERT_EQ(POLL, fb_poll(&bootimg));
}

TEST(Fastboot, TcpContinue) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());

  ASSERT_TRUE(tcp.SendData(TcpPacket("continue")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAY"));
  EXPECT_EQ(tcp.ClientDisconnect(), CONTINUE_BOOT);
}

TEST(Fastboot, TcpReboot) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());

  ASSERT_TRUE(tcp.SendData(TcpPacket("reboot")));
  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAY"));
  EXPECT_EQ(tcp.ClientDisconnect(), REBOOT);
}

TEST(Fastboot, TcpIsAvailable) {
  FakeFastbootUdp udp;
  FakeFastbootTcp tcp;

  // Not available before the drivers have initialized.
  ASSERT_FALSE(fb_tcp_is_available());

  // Run the loop once, drivers will initialize and TCP should be available.
  ASSERT_EQ(POLL, fb_poll(nullptr));
  ASSERT_TRUE(fb_tcp_is_available());

  // Should continue to be available as we progress through the session and
  // after disconnecting.
  ASSERT_NO_FATAL_FAILURE(tcp.InitializeSession());
  ASSERT_TRUE(fb_tcp_is_available());

  ASSERT_TRUE(tcp.SendData(TcpPacket("getvar:product")));
  ASSERT_TRUE(fb_tcp_is_available());

  ASSERT_EQ(tcp.ReceiveData(), TcpPacket("OKAYgigaboot"));
  ASSERT_TRUE(fb_tcp_is_available());

  EXPECT_EQ(tcp.ClientDisconnect(), POLL);
  ASSERT_TRUE(fb_tcp_is_available());
}

}  // namespace
}  // namespace gigaboot
