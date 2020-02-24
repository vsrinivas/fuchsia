// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_DRIVERS_OT_RADIO_OT_RADIO_BOOTLOADER_H_
#define SRC_CONNECTIVITY_OPENTHREAD_DRIVERS_OT_RADIO_OT_RADIO_BOOTLOADER_H_

#include "ot_radio.h"
#include "ot_radio_bootloader_spi_packets.h"

namespace ot {

enum OtRadioBlResult {
  BL_RET_SUCCESS,

  // spi transmit failure when sending any of the spi commands.
  // Check bl_zx_status for reason
  BL_ERR_SPI_TRANSMIT_FAILED = -1,

  // Final verification of bytes by sending a crc failed
  BL_ERR_VERIFICATION_FAILED = -2,

  // Syscall for reading fw bytes failed. Check bl_zx_status
  BL_ERR_GET_FW_BYTES_FAILED = -3,

  // Check bl_zx_status for exact reason of failure
  BL_ERR_PUT_RCP_BLMODE_FAIL = -4,

  // Check bl_zx_status for exact reason of failure
  BL_ERR_RESET_FAILED = -5,

  // Write cmd sent to bootloader returned with a valid response but non-zero
  // status. It can happen if we don't have permission to write to that region
  BL_ERR_WR_CMD_FAILED = -6,

  // Bootloader Response received doesn't match cmd field of the cmd sent
  BL_ERR_RESP_CMD_MISMATCH = -7,

  // Bootloader response recieved has incorrect crc
  BL_ERR_RESP_CRC_MISMATCH = -8,

  // Bootloader response received doesn't have expected length
  BL_ERR_RESP_LENGTH_MISMATCH = -9,

  // Port wait timed out when waiting for response
  BL_ERR_PORT_WAIT_TIMED_OUT = -10,

  // Port wait when waiting for response failed for some reason. Check
  // bl_zx_status
  BL_ERR_PORT_WAIT_FAILED = -11,

  // port wait received a packet with key EXIT_THREAD
  BL_ERR_PORT_KEY_EXIT_THREAD = -12,

  // Firmware is invalid, doesn't have proper version string
  BL_ERR_INVALID_FW_VERSION = -13
};

class OtRadioDeviceBootloader {
 public:
  OtRadioBlResult UpdateRadioFirmware();
  OtRadioBlResult GetBootloaderVersion(std::string& bl_version);
  zx_status_t PutRcpInBootloader();
  OtRadioDeviceBootloader(OtRadioDevice* dev) : dev_handle_(dev) {}
  zx_status_t GetLastZxStatus();

 private:
  OtRadioDevice* dev_handle_;
  zx_status_t GetFirmwareBytes(std::vector<uint8_t>* fw_bytes);
  OtRadioBlResult SendSpiCmdAndGetResponse(uint8_t* cmd, size_t cmd_size, size_t exp_resp_size);
  void PrintSpiCommand(int cmd, void* cmd_ptr, size_t cmd_size);
  OtRadioBlResult SendFlashEraseCmd(uint32_t address, uint32_t length);
  OtRadioBlResult UploadFirmware(const std::vector<uint8_t>& fw_bytes);
  OtRadioBlResult VerifyUpload(const std::vector<uint8_t>& fw_bytes);
  OtRadioBlResult UploadAndCheckFirmware(const std::vector<uint8_t>& fw_bytes);
  bool FirmwareAlreadyUpToDateCRC(const std::vector<uint8_t>& fw_bytes);
  uint32_t BlModeCrc32(uint32_t crc, const void* b, size_t len);
  void BlPrepareCmd(NlSpiBlBasicCmd* cmdp, uint16_t length, uint16_t cmd);

  // Address at which firmware is flashed
  static constexpr uint32_t kFwStartAddr = 0x5000;

  // A syscall may fail, resulting in some fuction failing. The failing
  // functions return type may not be zx_status_t. Store the last syscall
  // status in this variable which can be inspected by calling function
  // in case of failure
  zx_status_t bl_zx_status = ZX_OK;
};
}  // namespace ot

#endif  // SRC_CONNECTIVITY_OPENTHREAD_DRIVERS_OT_RADIO_OT_RADIO_BOOTLOADER_H_
