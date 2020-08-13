// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <string_view>

#include "test/paver-test-common.h"

namespace {

constexpr char kFakeData[] = "lalala";

// Returns a full firmware filename for the given type (no type by default).
std::string FirmwareFilename(const std::string& type = "") {
  return NB_FIRMWARE_FILENAME_PREFIX + type;
}

TEST(PaverTest, Constructor) { netsvc::Paver paver(zx::channel); }

TEST(PaverTest, GetSingleton) { ASSERT_NOT_NULL(netsvc::Paver::Get()); }

TEST(PaverTest, InitialInProgressFalse) {
  zx::channel chan;
  fbl::unique_fd fd;
  netsvc::Paver paver_(std::move(chan), std::move(fd));
  ASSERT_FALSE(paver_.InProgress());
}

TEST(PaverTest, InitialExitCodeValid) {
  zx::channel chan;
  fbl::unique_fd fd;
  netsvc::Paver paver_(std::move(chan), std::move(fd));
  ASSERT_OK(paver_.exit_code());
}

TEST_F(PaverTest, OpenWriteInvalidFile) {
  char invalid_file_name[32] = {};
  ASSERT_NE(paver_.OpenWrite(invalid_file_name, 0), TFTP_NO_ERROR);
  paver_.Close();
}

TEST_F(PaverTest, OpenWriteInvalidSize) {
  ASSERT_NE(paver_.OpenWrite(FirmwareFilename(), 0), TFTP_NO_ERROR);
}

TEST_F(PaverTest, OpenWriteValidFile) {
  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), 1024), TFTP_NO_ERROR);
  paver_.Close();
}

TEST_F(PaverTest, OpenTwice) {
  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), 1024), TFTP_NO_ERROR);
  ASSERT_NE(paver_.OpenWrite(FirmwareFilename(), 1024), TFTP_NO_ERROR);
  paver_.Close();
}

TEST_F(PaverTest, WriteWithoutOpen) {
  size_t size = sizeof(kFakeData);
  ASSERT_NE(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
}

TEST_F(PaverTest, WriteAfterClose) {
  size_t size = sizeof(kFakeData);
  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), 1024), TFTP_NO_ERROR);
  paver_.Close();
  // TODO(surajmalhotra): Should we ensure this fails?
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
}

TEST_F(PaverTest, TimeoutNoWrites) {
  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), 1024), TFTP_NO_ERROR);
  paver_.Close();
  Wait();
  ASSERT_NE(paver_.exit_code(), ZX_OK);
}

TEST_F(PaverTest, TimeoutPartialWrite) {
  size_t size = sizeof(kFakeData);
  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), 1024), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_NE(paver_.exit_code(), ZX_OK);
}

void ValidateCommandTrace(const std::vector<Command>& actual,
                          const std::vector<Command>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < actual.size(); i++) {
    EXPECT_EQ(actual[i], expected[i], "Command #%zu different from expected", i);
  }
}

TEST_F(PaverTest, WriteCompleteSingle) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWriteFirmware});
}

TEST_F(PaverTest, WriteCompleteManySmallWrites) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(1024);
  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), 1024), TFTP_NO_ERROR);
  for (size_t offset = 0; offset < 1024; offset += sizeof(kFakeData)) {
    size = std::min(sizeof(kFakeData), 1024 - offset);
    ASSERT_EQ(paver_.Write(kFakeData, &size, offset), TFTP_NO_ERROR);
    ASSERT_EQ(size, std::min(sizeof(kFakeData), 1024 - offset));
  }
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());
}

TEST_F(PaverTest, Overwrite) {
  size_t size = sizeof(kFakeData);
  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), 2), TFTP_NO_ERROR);
  ASSERT_NE(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  paver_.Close();
  Wait();
  ASSERT_NE(paver_.exit_code(), ZX_OK);
}

TEST_F(PaverTest, CloseChannelBetweenWrites) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(2 * size);
  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), 2 * size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  loop_.Shutdown();
  ASSERT_EQ(paver_.Write(kFakeData, &size, size), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_EQ(paver_.exit_code(), ZX_ERR_PEER_CLOSED);
}

TEST_F(PaverTest, WriteZirconA) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_ZIRCONA_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(),
                       {Command::kInitializeAbr, Command::kWriteAsset});
}

TEST_F(PaverTest, WriteVbMetaA) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_VBMETAA_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(),
                       {Command::kInitializeAbr, Command::kWriteAsset});
}

TEST_F(PaverTest, WriteZirconAWithABRSupported) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_abr_supported(true);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_ZIRCONA_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(),
                       {
                           Command::kInitializeAbr,
                           Command::kQueryActiveConfiguration,
                           Command::kSetConfigurationUnbootable,
                           Command::kWriteAsset,
                           Command::kBootManagerFlush,
                       });
}

TEST_F(PaverTest, WriteZirconBWithABRSupported) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_abr_supported(true);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_ZIRCONB_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(),
                       {
                           Command::kInitializeAbr,
                           Command::kQueryActiveConfiguration,
                           Command::kSetConfigurationUnbootable,
                           Command::kWriteAsset,
                           Command::kBootManagerFlush,
                       });
}

TEST_F(PaverTest, WriteZirconRWithABRSupported) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_abr_supported(true);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_ZIRCONR_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(),
                       {
                           Command::kInitializeAbr,
                           Command::kQueryActiveConfiguration,
                           Command::kWriteAsset,
                           Command::kBootManagerFlush,
                       });
}

TEST_F(PaverTest, WriteVbMetaAWithABRSupported) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_abr_supported(true);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_VBMETAA_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(),
                       {
                           Command::kInitializeAbr,
                           Command::kQueryActiveConfiguration,
                           Command::kSetConfigurationUnbootable,
                           Command::kWriteAsset,
                           Command::kSetConfigurationActive,
                           Command::kSetConfigurationUnbootable,
                           Command::kDataSinkFlush,
                           Command::kBootManagerFlush,
                       });
  ASSERT_FALSE(fake_svc_.fake_paver().abr_data().slot_a.unbootable);
  ASSERT_TRUE(fake_svc_.fake_paver().abr_data().slot_a.active);
  ASSERT_TRUE(fake_svc_.fake_paver().abr_data().slot_b.unbootable);
}

TEST_F(PaverTest, WriteVbMetaBWithABRSupported) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_abr_supported(true);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_VBMETAB_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(),
                       {
                           Command::kInitializeAbr,
                           Command::kQueryActiveConfiguration,
                           Command::kSetConfigurationUnbootable,
                           Command::kWriteAsset,
                           Command::kSetConfigurationActive,
                           Command::kSetConfigurationUnbootable,
                           Command::kDataSinkFlush,
                           Command::kBootManagerFlush,
                       });
  ASSERT_FALSE(fake_svc_.fake_paver().abr_data().slot_b.unbootable);
  ASSERT_TRUE(fake_svc_.fake_paver().abr_data().slot_b.active);
  ASSERT_TRUE(fake_svc_.fake_paver().abr_data().slot_a.unbootable);
}

TEST_F(PaverTest, WriteVbMetaRWithABRSupported) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_abr_supported(true);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_VBMETAR_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(),
                       {
                           Command::kInitializeAbr,
                           Command::kQueryActiveConfiguration,
                           Command::kWriteAsset,
                           Command::kBootManagerFlush,
                       });
}

TEST_F(PaverTest, WriteZirconAWithABRSupportedTwice) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_abr_supported(true);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  std::vector<Command> expected_per_time = {
      Command::kInitializeAbr,
      Command::kQueryActiveConfiguration,
      Command::kSetConfigurationUnbootable,
      Command::kWriteAsset,
      Command::kBootManagerFlush,
  };
  std::vector<Command> expected_accumulative;
  for (int i = 0; i < 2; i++) {
    ASSERT_EQ(paver_.OpenWrite(NB_ZIRCONA_FILENAME, size), TFTP_NO_ERROR);
    ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
    paver_.Close();
    Wait();
    ASSERT_OK(paver_.exit_code());
    expected_accumulative.insert(expected_accumulative.end(), expected_per_time.begin(),
                                 expected_per_time.end());
    ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), expected_accumulative);
  }
}

TEST_F(PaverTest, WriteSshAuth) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_SSHAUTH_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());
  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWriteDataFile});
}

TEST_F(PaverTest, WriteFvm) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_FVM_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());
  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWriteVolumes});
}

TEST_F(PaverTest, WriteFvmManySmallWrites) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(1024);
  ASSERT_EQ(paver_.OpenWrite(NB_FVM_FILENAME, 1024), TFTP_NO_ERROR);
  for (size_t offset = 0; offset < 1024; offset += sizeof(kFakeData)) {
    size = std::min(sizeof(kFakeData), 1024 - offset);
    ASSERT_EQ(paver_.Write(kFakeData, &size, offset), TFTP_NO_ERROR);
    ASSERT_EQ(size, std::min(sizeof(kFakeData), 1024 - offset));
  }
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());
  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWriteVolumes});
}

TEST_F(PaverTest, InitializePartitionTables) {
  ASSERT_NO_FAILURES(SpawnBlockDevice());

  modify_partition_table_info partition_info = {};
  strcpy(partition_info.block_device_path, "/dev/");
  strcat(partition_info.block_device_path, ramdisk_get_path(ramdisk_));

  size_t size = sizeof(partition_info);
  ASSERT_EQ(paver_.OpenWrite(NB_INIT_PARTITION_TABLES_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(&partition_info, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(partition_info));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());
  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kInitPartitionTables});
}

TEST_F(PaverTest, WipePartitionTables) {
  ASSERT_NO_FAILURES(SpawnBlockDevice());

  modify_partition_table_info partition_info = {};
  strcpy(partition_info.block_device_path, "/dev/");
  strcat(partition_info.block_device_path, ramdisk_get_path(ramdisk_));

  size_t size = sizeof(partition_info);
  ASSERT_EQ(paver_.OpenWrite(NB_WIPE_PARTITION_TABLES_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(&partition_info, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(partition_info));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());
  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWipePartitionTables});
}

TEST_F(PaverTest, WriteFirmwareNoType) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWriteFirmware});
  EXPECT_EQ(fake_svc_.fake_paver().last_firmware_type(), "");
}

TEST_F(PaverTest, WriteFirmwareSupportedType) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  fake_svc_.fake_paver().set_supported_firmware_type("foo");

  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename("foo"), size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWriteFirmware});
  EXPECT_EQ(fake_svc_.fake_paver().last_firmware_type(), "foo");
}

TEST_F(PaverTest, WriteFirmwareUnsupportedType) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  fake_svc_.fake_paver().set_supported_firmware_type("foo");

  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename("bar"), size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  // This should still return OK so that we just skip unknown firmware types.
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWriteFirmware});
  EXPECT_EQ(fake_svc_.fake_paver().last_firmware_type(), "bar");
}

TEST_F(PaverTest, WriteFirmwareFailure) {
  // Trigger an error by not writing enough data.
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size + 1);

  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(), size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  // This should not return OK since an actual error occurred.
  ASSERT_NOT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWriteFirmware});
}

TEST_F(PaverTest, WriteFirmwareTypeMaxLength) {
  const std::string type(NB_FIRMWARE_TYPE_MAX_LENGTH, 'a');
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  fake_svc_.fake_paver().set_supported_firmware_type(type);

  ASSERT_EQ(paver_.OpenWrite(FirmwareFilename(type), size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWriteFirmware});
  EXPECT_EQ(fake_svc_.fake_paver().last_firmware_type(), type);
}

TEST_F(PaverTest, WriteFirmwareTypeTooLong) {
  const std::string type(NB_FIRMWARE_TYPE_MAX_LENGTH + 1, 'a');
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);

  EXPECT_EQ(paver_.OpenWrite(FirmwareFilename(type), size), TFTP_ERR_INVALID_ARGS);
  EXPECT_NE(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  paver_.Close();
  Wait();

  // Make sure the WriteFirmware() call was never made.
  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {});
}

TEST_F(PaverTest, BootloaderUsesWriteFirmware) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());

  // Legacy BOOTLOADER file should use WriteFirmare() FIDL with empty type.
  ValidateCommandTrace(fake_svc_.fake_paver().GetCommandTrace(), {Command::kWriteFirmware});
  EXPECT_EQ(fake_svc_.fake_paver().last_firmware_type(), "");
}

}  // namespace
