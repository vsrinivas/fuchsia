// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fastboot/fastboot.h>
#include <lib/fidl-async/cpp/bind.h>

#include <unordered_map>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/storage/testing/fake-paver.h"

namespace fastboot {

namespace {

using Packets = std::vector<std::string>;

class TestTransport : public Transport {
 public:
  void AddInPacket(const void* data, size_t size) {
    const char* start = static_cast<const char*>(data);
    in_packets_.insert(in_packets_.begin(), std::string(start, start + size));
  }

  template <typename T>
  void AddInPacket(const T& container) {
    return AddInPacket(container.data(), container.size());
  }

  const Packets& GetOutPackets() { return out_packets_; }

  zx::status<size_t> ReceivePacket(void* dst, size_t capacity) override {
    if (in_packets_.empty()) {
      return zx::error(ZX_ERR_BAD_STATE);
    }

    const std::string& packet = in_packets_.back();
    if (packet.size() > capacity) {
      return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
    }

    size_t size = packet.size();
    memcpy(dst, packet.data(), size);
    in_packets_.pop_back();
    return zx::ok(size);
  }

  size_t PeekPacketSize() override { return in_packets_.empty() ? 0 : in_packets_.back().size(); }

  // Send a packet over the transport.
  zx::status<> Send(std::string_view packet) override {
    out_packets_.push_back(std::string(packet.data(), packet.size()));
    return zx::ok();
  }

 private:
  Packets in_packets_;
  Packets out_packets_;
};

void CheckPacketsEqual(const Packets& lhs, const Packets& rhs) {
  ASSERT_EQ(lhs.size(), rhs.size());
  for (size_t i = 0; i < lhs.size(); i++) {
    ASSERT_EQ(lhs[i], rhs[i]);
  }
}

TEST(FastbootTest, NoPacket) {
  Fastboot fastboot(0x40000);
  TestTransport transport;
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  Packets expected_packets = {};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

TEST(FastbootTest, GetVarMaxDownloadSize) {
  Fastboot fastboot(0x40000);
  const char command[] = "getvar:max-download-size";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  Packets expected_packets = {"OKAY0x00040000"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

TEST(FastbootTest, GetVarUnknownVariable) {
  Fastboot fastboot(0x40000);
  const char command[] = "getvar:unknown";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST(FastbootTest, GetVarNotEnoughArgument) {
  Fastboot fastboot(0x40000);
  const char command[] = "getvar";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST(FastbootTest, UnknownCommand) {
  Fastboot fastboot(0x40000);
  const char command[] = "Unknown";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

}  // namespace

class FastbootDownloadTest : public zxtest::Test {
 public:
  void DownloadData(Fastboot& fastboot, const std::vector<uint8_t>& download_content) {
    std::string size_hex_str = fxl::StringPrintf("%08zx", download_content.size());

    std::string command = "download:" + size_hex_str;
    TestTransport transport;
    transport.AddInPacket(command);
    zx::status<> ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());
    std::vector<std::string> expected_packets = {
        "DATA" + size_hex_str,
    };
    ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
    ASSERT_EQ(fastboot.download_vmo_mapper_.size(), download_content.size());
    // start the download

    // Transmit the first half.
    std::vector<uint8_t> first_half(download_content.begin(),
                                    download_content.begin() + download_content.size() / 2);
    transport.AddInPacket(first_half);
    ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());
    // There should be no new response packet.
    ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
    ASSERT_BYTES_EQ(fastboot.download_vmo_mapper_.start(), first_half.data(), first_half.size());

    // Transmit the second half
    std::vector<uint8_t> second_half(download_content.begin() + first_half.size(),
                                     download_content.end());
    transport.AddInPacket(second_half);
    ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());
    expected_packets.push_back("OKAY");
    ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
    ASSERT_BYTES_EQ(fastboot.download_vmo_mapper_.start(), download_content.data(),
                    download_content.size());
  }
};

namespace {

TEST_F(FastbootDownloadTest, DownloadSucceed) {
  Fastboot fastboot(0x40000);

  std::vector<uint8_t> download_content;
  for (size_t i = 0; i <= 0xff; i++) {
    download_content.push_back(static_cast<uint8_t>(i));
  }

  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
}

TEST_F(FastbootDownloadTest, DownloadCompleteResetState) {
  Fastboot fastboot(0x40000);

  std::vector<uint8_t> download_content;
  for (size_t i = 0; i <= std::numeric_limits<uint8_t>::max(); i++) {
    download_content.push_back(static_cast<uint8_t>(i));
  }

  // Test the download command twice. The second time is to test that Fastboot re-enter
  // the command waiting state after a complete download.
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
}

TEST(FastbootTest, DownloadFailsOnUnexpectedAmountOfData) {
  Fastboot fastboot(0x40000);

  std::vector<uint8_t> download_content;
  for (size_t i = 0; i <= std::numeric_limits<uint8_t>::max(); i++) {
    download_content.push_back(static_cast<uint8_t>(i));
  }

  std::string size_hex_str = fxl::StringPrintf("%08zx", download_content.size());

  std::string command = "download:" + size_hex_str;
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  // Transmit the first half.
  std::vector<uint8_t> first_half(download_content.begin(),
                                  download_content.begin() + download_content.size() / 2);
  transport.AddInPacket(first_half);
  ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  // The second transmit sends the entire download, which will exceed expected size.
  transport.AddInPacket(download_content);
  ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  // Check that the last packet is a FAIL response
  ASSERT_EQ(transport.GetOutPackets().size(), 2ULL);
  ASSERT_EQ(transport.GetOutPackets().back().compare(0, 4, "FAIL"), 0);
}

TEST(FastbootTest, DownloadFailsOnZeroSizeDownload) {
  Fastboot fastboot(0x40000);
  std::string command = "download:00000000";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST(FastbootTest, DownloadFailsOnNotEnoughArgument) {
  Fastboot fastboot(0x40000);
  std::string command = "download";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

class FastbootFlashTest : public FastbootDownloadTest {
 protected:
  FastbootFlashTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), vfs_(loop_.dispatcher()) {
    // Set up a svc root directory with a paver service entry.
    auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir->AddEntry(
        fidl::DiscoverableProtocolName<fuchsia_paver::Paver>,
        fbl::MakeRefCounted<fs::Service>([this](fidl::ServerEnd<fuchsia_paver::Paver> request) {
          return fake_paver_.Connect(loop_.dispatcher(), std::move(request));
        }));
    zx::status server_end = fidl::CreateEndpoints(&svc_local_);
    ASSERT_OK(server_end.status_value());
    vfs_.ServeDirectory(root_dir, std::move(server_end.value()));
    loop_.StartThread("fastboot-flash-test-loop");
  }

  fidl::ClientEnd<fuchsia_io::Directory>& svc_chan() { return svc_local_; }

  paver_test::FakePaver& paver() { return fake_paver_; }

  ~FastbootFlashTest() { loop_.Shutdown(); }

  void TestFlashBootloader(Fastboot& fastboot, fuchsia_paver::wire::Configuration config,
                           const std::string& type_suffix) {
    std::vector<uint8_t> download_content(256, 1);
    ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

    paver_test::FakePaver& fake_paver = paver();
    fake_paver.set_expected_payload_size(download_content.size());

    std::unordered_map<fuchsia_paver::wire::Configuration, std::string> config_to_partition = {
        {fuchsia_paver::wire::Configuration::kA, "bootloader_a"},
        {fuchsia_paver::wire::Configuration::kB, "bootloader_b"},
        {fuchsia_paver::wire::Configuration::kRecovery, "bootloader_r"},
    };

    TestTransport transport;
    std::string command = "flash:" + config_to_partition[config] + type_suffix;

    transport.AddInPacket(command);
    zx::status<> ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());

    std::vector<std::string> expected_packets = {"OKAY"};
    ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
    ASSERT_EQ(fake_paver.last_firmware_config(), config);
  }

  void TestFlashBootloaderNoFirmwareType(Fastboot& fastboot,
                                         fuchsia_paver::wire::Configuration config) {
    fake_paver_.set_supported_firmware_type("");
    ASSERT_NO_FATAL_FAILURE(TestFlashBootloader(fastboot, config, ""));
    ASSERT_EQ(fake_paver_.last_firmware_type(), "");
  }

  void TestFlashBootloaderWithFirmwareType(Fastboot& fastboot,
                                           fuchsia_paver::wire::Configuration config,
                                           const std::string& type) {
    fake_paver_.set_supported_firmware_type(type);
    ASSERT_NO_FATAL_FAILURE(TestFlashBootloader(fastboot, config, ":" + type));
    ASSERT_EQ(fake_paver_.last_firmware_type(), type);
  }

  void TestFlashAsset(const std::string partition, fuchsia_paver::wire::Configuration config,
                      fuchsia_paver::wire::Asset asset) {
    Fastboot fastboot(0x40000, std::move(svc_chan()));
    std::vector<uint8_t> download_content(256, 1);
    ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
    paver_test::FakePaver& fake_paver = paver();
    fake_paver.set_expected_payload_size(download_content.size());

    std::string command = "flash:" + partition;
    TestTransport transport;
    transport.AddInPacket(command);
    zx::status<> ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());

    std::vector<std::string> expected_packets = {"OKAY"};
    ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
    ASSERT_EQ(fake_paver.last_asset_config(), config);
    ASSERT_EQ(fake_paver.last_asset(), asset);
  }

  void TestSetActive(const std::string slot) {
    Fastboot fastboot(0x40000, std::move(svc_chan()));
    paver().set_abr_supported(true);

    TestTransport transport;
    const std::string command = "set_active:" + slot;
    transport.AddInPacket(command);
    zx::status<> ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());

    std::vector<std::string> expected_packets = {"OKAY"};
    ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
  }

  async::Loop loop_;
  fs::SynchronousVfs vfs_;
  paver_test::FakePaver fake_paver_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_local_;
};

TEST_F(FastbootFlashTest, FlashFailsOnNotEnoughArguments) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));

  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  std::string command = "flash";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, FlashFailsOnUnsupportedPartition) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));

  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  std::string command = "flash:unknown-partition";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, FlashBooloaderASlot) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  ASSERT_NO_FATAL_FAILURE(
      TestFlashBootloaderNoFirmwareType(fastboot, fuchsia_paver::wire::Configuration::kA));
}

TEST_F(FastbootFlashTest, FlashBooloaderBSlot) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  ASSERT_NO_FATAL_FAILURE(
      TestFlashBootloaderNoFirmwareType(fastboot, fuchsia_paver::wire::Configuration::kB));
}

TEST_F(FastbootFlashTest, FlashBooloaderRSlot) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  ASSERT_NO_FATAL_FAILURE(
      TestFlashBootloaderNoFirmwareType(fastboot, fuchsia_paver::wire::Configuration::kRecovery));
}

TEST_F(FastbootFlashTest, FlashBooloaderASlotWithFirmwareType) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  ASSERT_NO_FATAL_FAILURE(TestFlashBootloaderWithFirmwareType(
      fastboot, fuchsia_paver::wire::Configuration::kA, "firmware_type"));
}

TEST_F(FastbootFlashTest, FlashBooloaderBSlotWithFirmwareType) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  ASSERT_NO_FATAL_FAILURE(TestFlashBootloaderWithFirmwareType(
      fastboot, fuchsia_paver::wire::Configuration::kB, "firmware_type"));
}

TEST_F(FastbootFlashTest, FlashBooloaderRSlotWithFirmwareType) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  ASSERT_NO_FATAL_FAILURE(TestFlashBootloaderWithFirmwareType(
      fastboot, fuchsia_paver::wire::Configuration::kRecovery, "firmware_type"));
}

TEST_F(FastbootFlashTest, FlashBooloaderWriteFail) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));

  // Insert a write firmware error
  paver_test::FakePaver& fake_paver = paver();
  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
  fake_paver.set_expected_payload_size(0);

  std::string command = "flash:bootloader_a";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_FALSE(ret.is_ok());

  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, FlashBooloaderUnsupportedFirmwareType) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));

  // Insert an unsupported firmware failure
  paver().set_supported_firmware_type("unsupported");

  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  std::string command = "flash:bootloader_a";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, FlashAssetZirconA) {
  TestFlashAsset("zircon_a", fuchsia_paver::wire::Configuration::kA,
                 fuchsia_paver::wire::Asset::kKernel);
}

TEST_F(FastbootFlashTest, FlashAssetZirconB) {
  TestFlashAsset("zircon_b", fuchsia_paver::wire::Configuration::kB,
                 fuchsia_paver::wire::Asset::kKernel);
}

TEST_F(FastbootFlashTest, FlashAssetZirconR) {
  TestFlashAsset("zircon_r", fuchsia_paver::wire::Configuration::kRecovery,
                 fuchsia_paver::wire::Asset::kKernel);
}

TEST_F(FastbootFlashTest, FlashAssetVerifiedBootMetadataA) {
  TestFlashAsset("vbmeta_a", fuchsia_paver::wire::Configuration::kA,
                 fuchsia_paver::wire::Asset::kVerifiedBootMetadata);
}

TEST_F(FastbootFlashTest, FlashAssetVerifiedBootMetadataB) {
  TestFlashAsset("vbmeta_b", fuchsia_paver::wire::Configuration::kB,
                 fuchsia_paver::wire::Asset::kVerifiedBootMetadata);
}

TEST_F(FastbootFlashTest, FlashAssetVerifiedBootMetadataR) {
  TestFlashAsset("vbmeta_r", fuchsia_paver::wire::Configuration::kRecovery,
                 fuchsia_paver::wire::Asset::kVerifiedBootMetadata);
}

TEST_F(FastbootFlashTest, FlashAssetFail) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
  // Trigger an internal error by using an incorrect size
  paver().set_expected_payload_size(128);

  std::string command = "flash:zircon_a";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_FALSE(ret.is_ok());
  ASSERT_EQ(transport.GetOutPackets().back().compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, SetActiveSlotA) {
  TestSetActive("a");
  ASSERT_TRUE(paver().abr_data().slot_a.active);
}

TEST_F(FastbootFlashTest, SetActiveSlotB) {
  TestSetActive("b");
  ASSERT_TRUE(paver().abr_data().slot_b.active);
}

TEST_F(FastbootFlashTest, SetActiveInvalidSlot) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "set_active:r";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

}  // namespace

}  // namespace fastboot
