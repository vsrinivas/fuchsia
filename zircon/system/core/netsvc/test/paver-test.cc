// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
#include "test/paver-test-common.h"

namespace {
constexpr char kFakeData[] = "lalala";
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
  ASSERT_NE(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 0), TFTP_NO_ERROR);
}

TEST_F(PaverTest, OpenWriteValidFile) {
  ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
  paver_.Close();
}

TEST_F(PaverTest, OpenTwice) {
  ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
  ASSERT_NE(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
  paver_.Close();
}

TEST_F(PaverTest, WriteWithoutOpen) {
  size_t size = sizeof(kFakeData);
  ASSERT_NE(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
}

TEST_F(PaverTest, WriteAfterClose) {
  size_t size = sizeof(kFakeData);
  ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
  paver_.Close();
  // TODO(surajmalhotra): Should we ensure this fails?
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
}

TEST_F(PaverTest, TimeoutNoWrites) {
  ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
  paver_.Close();
  Wait();
  ASSERT_NE(paver_.exit_code(), ZX_OK);
}

TEST_F(PaverTest, TimeoutPartialWrite) {
  size_t size = sizeof(kFakeData);
  ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_NE(paver_.exit_code(), ZX_OK);
}

TEST_F(PaverTest, WriteCompleteSingle) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, size), TFTP_NO_ERROR);
  ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  ASSERT_EQ(size, sizeof(kFakeData));
  paver_.Close();
  Wait();
  ASSERT_OK(paver_.exit_code());
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteBootloader);
}

TEST_F(PaverTest, WriteCompleteManySmallWrites) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(1024);
  ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
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
  ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 2), TFTP_NO_ERROR);
  ASSERT_NE(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
  paver_.Close();
  Wait();
  ASSERT_NE(paver_.exit_code(), ZX_OK);
}

TEST_F(PaverTest, CloseChannelBetweenWrites) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_expected_payload_size(2 * size);
  ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 2 * size), TFTP_NO_ERROR);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteAsset);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteAsset);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteAsset);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteAsset);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteAsset);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kSetConfigurationActive);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteAsset);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteAsset);
}

TEST_F(PaverTest, WriteZirconAWithABRSupportedTwice) {
  size_t size = sizeof(kFakeData);
  fake_svc_.fake_paver().set_abr_supported(true);
  fake_svc_.fake_paver().set_expected_payload_size(size);
  for (int i = 0; i < 2; i++) {
    ASSERT_EQ(paver_.OpenWrite(NB_ZIRCONA_FILENAME, size), TFTP_NO_ERROR);
    ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
    paver_.Close();
    Wait();
    ASSERT_OK(paver_.exit_code());
    ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteAsset);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteDataFile);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteVolumes);
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
  ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteVolumes);
}
