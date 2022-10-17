// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.buildinfo/cpp/wire.h>
#include <fidl/fuchsia.buildinfo/cpp/wire_test_base.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.fshost/cpp/wire_test_base.h>
#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fastboot/fastboot.h>
#include <lib/fastboot/test/test-transport.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <unordered_map>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

#include "sparse_format.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/storage/testing/fake-paver.h"

namespace fastboot {

namespace {

void CheckPacketsEqual(const Packets& lhs, const Packets& rhs) {
  ASSERT_EQ(lhs.size(), rhs.size());
  for (size_t i = 0; i < lhs.size(); i++) {
    ASSERT_EQ(lhs[i], rhs[i]);
  }
}

TEST(FastbootTest, NoPacket) {
  Fastboot fastboot(0x40000);
  TestTransport transport;
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  Packets expected_packets = {};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

TEST(FastbootTest, GetVarMaxDownloadSize) {
  Fastboot fastboot(0x40000);
  const char command[] = "getvar:max-download-size";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  Packets expected_packets = {"OKAY0x00040000"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

TEST(FastbootTest, GetVarUnknownVariable) {
  Fastboot fastboot(0x40000);
  const char command[] = "getvar:unknown";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::result<> ret = fastboot.ProcessPacket(&transport);
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
  zx::result<> ret = fastboot.ProcessPacket(&transport);
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
  zx::result<> ret = fastboot.ProcessPacket(&transport);
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
    zx::result<> ret = fastboot.ProcessPacket(&transport);
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
  // Make sure that all states are reset.
  ASSERT_EQ(fastboot.remaining_download_size(), 0ULL);
  ASSERT_EQ(fastboot.state(), FastbootBase::State::kCommand);

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
  zx::result<> ret = fastboot.ProcessPacket(&transport);
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

  ASSERT_EQ(fastboot.total_download_size(), 0ULL);
  ASSERT_EQ(fastboot.remaining_download_size(), 0ULL);
  ASSERT_EQ(fastboot.state(), FastbootBase::State::kCommand);
}

TEST(FastbootTest, DownloadFailsOnZeroSizeDownload) {
  Fastboot fastboot(0x40000);
  std::string command = "download:00000000";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);

  ASSERT_EQ(fastboot.total_download_size(), 0ULL);
  ASSERT_EQ(fastboot.remaining_download_size(), 0ULL);
  ASSERT_EQ(fastboot.state(), FastbootBase::State::kCommand);
}

TEST(FastbootTest, DownloadFailsOnNotEnoughArgument) {
  Fastboot fastboot(0x40000);
  std::string command = "download";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);

  ASSERT_EQ(fastboot.total_download_size(), 0ULL);
  ASSERT_EQ(fastboot.remaining_download_size(), 0ULL);
  ASSERT_EQ(fastboot.state(), FastbootBase::State::kCommand);
}

class FastbootFailGetDownloadBuffer : public Fastboot {
 public:
  using Fastboot::Fastboot;

 private:
  zx::result<void*> GetDownloadBuffer(size_t total_download_size) override {
    return zx::error(ZX_ERR_UNAVAILABLE);
  }
};

TEST(FastbootTest, DownloadFailsOnetDownloadBuffer) {
  FastbootFailGetDownloadBuffer fastboot(0x40000);

  std::vector<uint8_t> download_content;
  for (size_t i = 0; i <= std::numeric_limits<uint8_t>::max(); i++) {
    download_content.push_back(static_cast<uint8_t>(i));
  }

  std::string size_hex_str = fxl::StringPrintf("%08zx", download_content.size());

  std::string command = "download:" + size_hex_str;
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_error());

  // Check that the last packet is a FAIL response
  ASSERT_EQ(transport.GetOutPackets().size(), 1ULL);
  ASSERT_EQ(transport.GetOutPackets().back().compare(0, 4, "FAIL"), 0);

  ASSERT_EQ(fastboot.total_download_size(), 0ULL);
  ASSERT_EQ(fastboot.remaining_download_size(), 0ULL);
  ASSERT_EQ(fastboot.state(), FastbootBase::State::kCommand);
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
    zx::result server_end = fidl::CreateEndpoints(&svc_local_);
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
    zx::result<> ret = fastboot.ProcessPacket(&transport);
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
    zx::result<> ret = fastboot.ProcessPacket(&transport);
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
    zx::result<> ret = fastboot.ProcessPacket(&transport);
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
  zx::result<> ret = fastboot.ProcessPacket(&transport);
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
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, FlashBootloaderNoAbrNoFirmwareType) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));

  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  paver_test::FakePaver& fake_paver = paver();
  fake_paver.set_expected_payload_size(download_content.size());

  TestTransport transport;
  std::string command = "flash:bootloader";

  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  std::vector<std::string> expected_packets = {"OKAY"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
  ASSERT_EQ(fake_paver.last_firmware_config(), fuchsia_paver::wire::Configuration::kA);
}

TEST_F(FastbootFlashTest, FlashBootloaderNoAbrWithFirmwareType) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  paver_test::FakePaver& fake_paver = paver();
  fake_paver.set_expected_payload_size(download_content.size());

  std::string firmware_type = "firmware_type";
  fake_paver.set_supported_firmware_type(firmware_type);

  TestTransport transport;
  std::string command = "flash:bootloader:" + firmware_type;

  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  std::vector<std::string> expected_packets = {"OKAY"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
  ASSERT_EQ(fake_paver.last_firmware_config(), fuchsia_paver::wire::Configuration::kA);
  ASSERT_EQ(fake_paver_.last_firmware_type(), firmware_type);
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
  zx::result<> ret = fastboot.ProcessPacket(&transport);
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
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, FlashFuchsiaEsp) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));

  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  paver_test::FakePaver& fake_paver = paver();
  fake_paver.set_expected_payload_size(download_content.size());

  std::string command = "flash:fuchsia-esp";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  std::vector<std::string> expected_packets = {"OKAY"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));

  ASSERT_EQ(fake_paver.last_firmware_config(), fuchsia_paver::wire::Configuration::kA);
  ASSERT_EQ(fake_paver.last_firmware_type(), "");
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

TEST_F(FastbootFlashTest, FlashAssetLegacyZirconA) {
  TestFlashAsset("zircon-a", fuchsia_paver::wire::Configuration::kA,
                 fuchsia_paver::wire::Asset::kKernel);
}

TEST_F(FastbootFlashTest, FlashAssetLegacyZirconB) {
  TestFlashAsset("zircon-b", fuchsia_paver::wire::Configuration::kB,
                 fuchsia_paver::wire::Asset::kKernel);
}

TEST_F(FastbootFlashTest, FlashAssetLegacyZirconR) {
  TestFlashAsset("zircon-r", fuchsia_paver::wire::Configuration::kRecovery,
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
  zx::result<> ret = fastboot.ProcessPacket(&transport);
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
  paver().set_abr_supported(true);
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "set_active:r";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, FlashFVM) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));

  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  paver_test::FakePaver& fake_paver = paver();
  fake_paver.set_expected_payload_size(download_content.size());

  std::string command = "flash:fvm.sparse";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  std::vector<std::string> expected_packets = {"OKAY"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
  ASSERT_EQ(fake_paver.GetCommandTrace(),
            std::vector<paver_test::Command>{paver_test::Command::kWriteVolumes});
}

TEST_F(FastbootFlashTest, GetVarSlotCount) {
  paver().set_abr_supported(true);
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "getvar:slot-count";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  std::vector<std::string> expected_packets = {"OKAY2"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

TEST_F(FastbootFlashTest, GetVarSlotCountAbrNotSupported) {
  paver().set_abr_supported(false);
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "getvar:slot-count";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  std::vector<std::string> expected_packets = {"OKAY1"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

TEST_F(FastbootFlashTest, GetVarIsUserspace) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "getvar:is-userspace";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  std::vector<std::string> expected_packets = {"OKAYyes"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

class FastbootRebootTest : public zxtest::Test,
                           public fidl::WireServer<fuchsia_hardware_power_statecontrol::Admin> {
 public:
  FastbootRebootTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), vfs_(loop_.dispatcher()) {
    // Set up a svc root directory with a power state control service entry.
    auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir->AddEntry(
        fidl::DiscoverableProtocolName<fuchsia_hardware_power_statecontrol::Admin>,
        fbl::MakeRefCounted<fs::Service>(
            [this](fidl::ServerEnd<fuchsia_hardware_power_statecontrol::Admin> request) {
              return fidl::BindSingleInFlightOnly<
                  fidl::WireServer<fuchsia_hardware_power_statecontrol::Admin>>(
                  loop_.dispatcher(), std::move(request), this);
            }));
    zx::result server_end = fidl::CreateEndpoints(&svc_local_);
    ASSERT_OK(server_end.status_value());
    vfs_.ServeDirectory(root_dir, std::move(server_end.value()));
    loop_.StartThread("fastboot-reboot-test-loop");
  }

  ~FastbootRebootTest() { loop_.Shutdown(); }

  fidl::ClientEnd<fuchsia_io::Directory>& svc_chan() { return svc_local_; }
  bool reboot_triggered() { return reboot_triggered_; }
  bool reboot_recovery_triggered() { return reboot_recovery_triggered_; }

 private:
  void Reboot(RebootRequestView request, RebootCompleter::Sync& completer) override {
    reboot_triggered_ = true;
    completer.ReplySuccess();
  }

  void RebootToRecovery(RebootToRecoveryCompleter::Sync& completer) override {
    reboot_recovery_triggered_ = true;
    completer.ReplySuccess();
  }

  void PowerFullyOn(PowerFullyOnCompleter::Sync& completer) override {}
  void RebootToBootloader(RebootToBootloaderCompleter::Sync& completer) override {}
  void Poweroff(PoweroffCompleter::Sync& completer) override {}
  void Mexec(MexecRequestView request, MexecCompleter::Sync& completer) override {}
  void SuspendToRam(SuspendToRamCompleter::Sync& completer) override {}

  async::Loop loop_;
  fs::SynchronousVfs vfs_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_local_;

  bool reboot_triggered_ = false;
  bool reboot_recovery_triggered_ = false;
};

TEST_F(FastbootRebootTest, Reboot) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "reboot";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  std::vector<std::string> expected_packets = {"OKAY"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
  ASSERT_TRUE(reboot_triggered());
}

TEST_F(FastbootRebootTest, Continue) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "continue";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  // One info message plus one OKAY message
  ASSERT_EQ(transport.GetOutPackets().size(), 2ULL);
  ASSERT_EQ(transport.GetOutPackets().back(), "OKAY");
  ASSERT_TRUE(reboot_triggered());
}

TEST_F(FastbootRebootTest, RebootBootloader) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "reboot-bootloader";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  // One info message plus one OKAY message
  ASSERT_EQ(transport.GetOutPackets().size(), 2ULL);
  ASSERT_EQ(transport.GetOutPackets().back(), "OKAY");
  ASSERT_TRUE(reboot_recovery_triggered());
}

TEST_F(FastbootFlashTest, UnknownOemCommand) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "oem unknown";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

class FastbootFshostTest : public FastbootDownloadTest,
                           public fidl::testing::WireTestBase<fuchsia_fshost::Admin> {
 public:
  FastbootFshostTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        outgoing_(component::OutgoingDirectory::Create(loop_.dispatcher())) {
    ASSERT_OK(outgoing_.AddProtocol<fuchsia_fshost::Admin>(this));
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_TRUE(endpoints.is_ok());
    ASSERT_EQ(ZX_OK, outgoing_.Serve(std::move(endpoints->server)).status_value());
    auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_TRUE(svc_endpoints.is_ok());
    ASSERT_OK(fidl::WireCall(endpoints->client)
                  ->Open(fuchsia_io::wire::OpenFlags::kRightWritable |
                             fuchsia_io::wire::OpenFlags::kRightReadable,
                         0, "svc",
                         fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeChannel())));
    svc_local_ = std::move(svc_endpoints->client);
    loop_.StartThread("fastboot-fshost-test-loop");
  }

  ~FastbootFshostTest() { loop_.Shutdown(); }

  fidl::ClientEnd<fuchsia_io::Directory>& svc_chan() {
    fbl::AutoLock al(&lock_);
    return svc_local_;
  }

  const std::string& data_file_name() {
    fbl::AutoLock al(&lock_);
    return data_file_name_;
  }

  const std::string& data_file_content() {
    fbl::AutoLock al(&lock_);
    return data_file_content_;
  }

  uint64_t data_file_vmo_content_size() {
    fbl::AutoLock al(&lock_);
    return data_file_vmo_content_size_;
  }

 private:
  void WriteDataFile(WriteDataFileRequestView request,
                     WriteDataFileCompleter::Sync& completer) override {
    fbl::AutoLock al(&lock_);
    data_file_name_ = std::string(request->filename.data(), request->filename.size());
    uint64_t size;
    ASSERT_OK(request->payload.get_size(&size));
    data_file_content_.resize(size);
    ASSERT_OK(request->payload.read(data_file_content_.data(), 0, size));
    ASSERT_OK(request->payload.get_prop_content_size(&data_file_vmo_content_size_));
    completer.ReplySuccess();
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    FAIL("Unexpected call to ControllerImpl: %s", name.c_str());
  }

  async::Loop loop_;
  component::OutgoingDirectory outgoing_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_local_;

  std::string data_file_name_ TA_GUARDED(lock_);
  std::string data_file_content_ TA_GUARDED(lock_);
  uint64_t data_file_vmo_content_size_ TA_GUARDED(lock_);

  mutable fbl::Mutex lock_;
};

TEST_F(FastbootFshostTest, OemAddStagedBootloaderFile) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  std::string command =
      "oem add-staged-bootloader-file " + std::string(sshd_host::kAuthorizedKeysBootloaderFileName);
  TestTransport transport;

  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  std::vector<std::string> expected_packets = {"OKAY"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));

  ASSERT_EQ(data_file_name(), sshd_host::kAuthorizedKeyPathInData);
  ASSERT_EQ(data_file_vmo_content_size(), download_content.size());
  ASSERT_BYTES_EQ(data_file_content().data(), download_content.data(), download_content.size());
}

TEST_F(FastbootFlashTest, OemAddStagedBootloaderFileInvalidNumberOfArguments) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "oem add-staged-bootloader-file";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, OemAddStagedBootloaderFileUnsupportedFile) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::string command = "oem add-staged-bootloader-file unknown";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, FlashRawFVM) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
  paver_test::FakePaver& fake_paver = paver();
  fake_paver.set_expected_payload_size(download_content.size());

  std::string command = "flash:fvm";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  std::vector<std::string> expected_packets = {"OKAY"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

TEST_F(FastbootFlashTest, FlashRawFVMFail) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  std::vector<uint8_t> download_content(256, 1);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
  paver_test::FakePaver& fake_paver = paver();

  // Use an incorrect size to trigger an error
  fake_paver.set_expected_payload_size(download_content.size() + 1);

  std::string command = "flash:fvm";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_error());

  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, AndroidSparseImageNotSupported) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  sparse_header_t header{
      .magic = SPARSE_HEADER_MAGIC,
  };
  const uint8_t* header_ptr = reinterpret_cast<const uint8_t*>(&header);
  std::vector<uint8_t> download_content(header_ptr, header_ptr + sizeof(header));
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
  paver_test::FakePaver& fake_paver = paver();
  fake_paver.set_expected_payload_size(download_content.size());

  std::string command = "flash:fvm";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST(FastbootBase, ExtractCommandArgsMultipleArgs) {
  FastbootBase::CommandArgs args;
  FastbootBase::ExtractCommandArgs("cmd:arg1:arg2:arg3:a", ":", args);

  EXPECT_EQ(args.num_args, 5);
  EXPECT_EQ(args.args[0], "cmd");
  EXPECT_EQ(args.args[1], "arg1");
  EXPECT_EQ(args.args[2], "arg2");
  EXPECT_EQ(args.args[3], "arg3");
  EXPECT_EQ(args.args[4], "a");
  EXPECT_EQ(args.args[5], "");
}

TEST(FastbootBase, ExtractCommandArgsNoArgs) {
  FastbootBase::CommandArgs args;
  FastbootBase::ExtractCommandArgs("cmd", ":", args);

  EXPECT_EQ(args.num_args, 1);
  EXPECT_EQ(args.args[0], "cmd");
  EXPECT_EQ(args.args[1], "");
}

TEST(FastbootBase, ExtractCommandArgsMiddleEmptyArgs) {
  FastbootBase::CommandArgs args;
  FastbootBase::ExtractCommandArgs("cmd::arg2", ":", args);

  EXPECT_EQ(args.num_args, 2);
  EXPECT_EQ(args.args[0], "cmd");
  EXPECT_EQ(args.args[1], "arg2");
}

TEST(FastbootBase, ExtractCommandArgsEndEmptyArgs) {
  FastbootBase::CommandArgs args;
  FastbootBase::ExtractCommandArgs("cmd:arg1:", ":", args);

  EXPECT_EQ(args.num_args, 2);
  EXPECT_EQ(args.args[0], "cmd");
  EXPECT_EQ(args.args[1], "arg1");
}

TEST(FastbootBase, ExtractCommandArgsMultipleBySpace) {
  FastbootBase::CommandArgs args;
  FastbootBase::ExtractCommandArgs("cmd arg1 arg2 arg3", " ", args);

  EXPECT_EQ(args.num_args, 4);
  EXPECT_EQ(args.args[0], "cmd");
  EXPECT_EQ(args.args[1], "arg1");
  EXPECT_EQ(args.args[2], "arg2");
  EXPECT_EQ(args.args[3], "arg3");
  EXPECT_EQ(args.args[4], "");
}

constexpr char kTestBoardConfig[] = "test-board-config";

class FastbootBuildInfoTest : public FastbootDownloadTest,
                              public fidl::testing::WireTestBase<fuchsia_buildinfo::Provider> {
 public:
  FastbootBuildInfoTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        outgoing_(component::OutgoingDirectory::Create(loop_.dispatcher())) {
    ASSERT_OK(outgoing_.AddProtocol<fuchsia_buildinfo::Provider>(this));
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_TRUE(endpoints.is_ok());
    ASSERT_EQ(ZX_OK, outgoing_.Serve(std::move(endpoints->server)).status_value());
    auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_TRUE(svc_endpoints.is_ok());
    ASSERT_OK(fidl::WireCall(endpoints->client)
                  ->Open(fuchsia_io::wire::OpenFlags::kRightWritable |
                             fuchsia_io::wire::OpenFlags::kRightReadable,
                         0, "svc",
                         fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeChannel())));
    svc_local_ = std::move(svc_endpoints->client);
    loop_.StartThread("fastboot-buildinfo-test-loop");
  }

  ~FastbootBuildInfoTest() { loop_.Shutdown(); }

  fidl::ClientEnd<fuchsia_io::Directory>& svc_chan() { return svc_local_; }

 private:
  void GetBuildInfo(GetBuildInfoCompleter::Sync& completer) override {
    fidl::Arena arena;
    auto res = fuchsia_buildinfo::wire::BuildInfo::Builder(arena)
                   .board_config(fidl::StringView(kTestBoardConfig))
                   .Build();
    completer.Reply(res);
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    FAIL("Unexpected call to BuildInfo: %s", name.c_str());
  }

  async::Loop loop_;
  component::OutgoingDirectory outgoing_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_local_;
};

TEST_F(FastbootBuildInfoTest, GetVarHwRevision) {
  Fastboot fastboot(0x40000, std::move(svc_chan()));
  const char command[] = "getvar:hw-revision";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::result<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  Packets expected_packets = {"OKAY" + std::string(kTestBoardConfig)};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

}  // namespace

}  // namespace fastboot
