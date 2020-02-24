// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ot_radio_bootloader.h"

#include <ctype.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/driver-unit-test/utils.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <iostream>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>

namespace ot {

// mimic device CRC32 computation in bootloader mode
constexpr uint32_t kCrcPolynomial = 0xEDB88320L;
uint32_t OtRadioDeviceBootloader::BlModeCrc32(uint32_t crc, const void *b, size_t len) {
  const uint8_t *buf = reinterpret_cast<const uint8_t *>(b);
  crc = ~crc;
  while (len--) {
    int j;
    uint8_t c = *buf++;
    crc = crc ^ c;
    for (j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (-(int)(crc & 1) & kCrcPolynomial);
    }
  }
  return ~crc;
}

zx_status_t OtRadioDeviceBootloader::GetLastZxStatus() { return bl_zx_status; }

void OtRadioDeviceBootloader::BlPrepareCmd(NlSpiBlBasicCmd *cmdp, uint16_t length, uint16_t cmd) {
  cmdp->length = length;
  cmdp->cmd = cmd;
  cmdp->crc32 = BlModeCrc32(kSpiPacketCrc32InitValue, &cmdp->length, length - sizeof(cmdp->crc32));
}

void OtRadioDeviceBootloader::PrintSpiCommand(int cmd, void *cmd_ptr, size_t cmd_size) {
  zxlogf(TRACE, "ot-radio: spi command ");

  switch (cmd) {
    case BL_SPI_CMD_INVALID:
      zxlogf(TRACE, "BL_SPI_CMD_INVALID: ");
      break;
    case BL_SPI_CMD_GET_VERSION:
      zxlogf(TRACE, "BL_SPI_CMD_GET_VERSION: ");
      break;
    case BL_SPI_CMD_UNUSED:
      zxlogf(TRACE, "BL_SPI_CMD_UNUSED: ");
      break;
    case BL_SPI_CMD_FLASH_ERASE:
      zxlogf(TRACE, "BL_SPI_CMD_FLASH_ERASE: ");
      break;
    case BL_SPI_CMD_FLASH_WRITE:
      zxlogf(TRACE, "BL_SPI_CMD_FLASH_WRITE: ");
      break;
    case BL_SPI_CMD_FLASH_VERIFY:
      zxlogf(TRACE, "BL_SPI_CMD_FLASH_VERIFY: ");
      break;
    default:
      zxlogf(TRACE, "UNKNOWN CMD: ");
  }

  unsigned long i;
  uint8_t *cmd_buf = reinterpret_cast<uint8_t *>(cmd_ptr);
  for (i = 0; i < cmd_size; i++) {
    zxlogf(TRACE, "0x%x ", cmd_buf[i]);
  }
  zxlogf(TRACE, "\n");
}

OtRadioBlResult OtRadioDeviceBootloader::SendSpiCmdAndGetResponse(uint8_t *cmd, size_t cmd_size,
                                                                  size_t exp_resp_size) {
  zxlogf(TRACE, "ot-radio: Sending command:\n");
  PrintSpiCommand(reinterpret_cast<NlSpiBlBasicCmd *>(cmd)->cmd, cmd, cmd_size);

  zx_status_t status = dev_handle_->spi_.Transmit(cmd, cmd_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: spi transmit failed with status : %d for cmd : %d\n", status,
           reinterpret_cast<NlSpiBlBasicCmd *>(cmd)->cmd);
    bl_zx_status = status;
    return BL_ERR_SPI_TRANSMIT_FAILED;
  }

  const uint32_t wait_time_sec = 10;

  while (1) {
    zx_port_packet_t packet = {};
    auto status = dev_handle_->port_.wait(zx::deadline_after(zx::sec(wait_time_sec)), &packet);

    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "ot-radio: port wait timed out: %d\n", status);
      bl_zx_status = ZX_ERR_TIMED_OUT;
      return BL_ERR_PORT_WAIT_TIMED_OUT;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "ot-radio: port wait failed: %d\n", status);
      bl_zx_status = status;
      return BL_ERR_PORT_WAIT_FAILED;
    }

    if (packet.key == PORT_KEY_EXIT_THREAD) {
      zxlogf(ERROR, "ot-radio: port key exit thread received\n");
      return BL_ERR_PORT_KEY_EXIT_THREAD;
    } else if (packet.key == PORT_KEY_RADIO_IRQ) {
      dev_handle_->interrupt_.ack();
      zxlogf(TRACE, "ot-radio: interrupt\n");

      // Read packet
      uint8_t i;
      size_t rx_actual;
      size_t read_length = exp_resp_size;
      dev_handle_->spi_.Receive(read_length, &dev_handle_->spi_rx_buffer_[0], read_length,
                                &rx_actual);
      zxlogf(TRACE, "ot-radio: rx_actual %lu expected : %lu\n", rx_actual, read_length);
      for (i = 0; i < read_length; i++) {
        zxlogf(TRACE, "ot-radio: RX %2X %c\n", dev_handle_->spi_rx_buffer_[i],
               isalnum(dev_handle_->spi_rx_buffer_[i]) ? dev_handle_->spi_rx_buffer_[i] : '#');
      }

      // Extract cmd field out of cmd that was sent
      uint8_t cmd_sent = reinterpret_cast<NlSpiBlBasicCmd *>(cmd)->cmd;

      // Check if response corresponds to appropriate command.
      // Reinterpret as cmd as first field in basic_response is basic_cmd, which
      // contains all the necessary fields
      NlSpiBlBasicCmd *response =
          reinterpret_cast<NlSpiBlBasicCmd *>(&dev_handle_->spi_rx_buffer_[0]);

      // Special case to ignore -- in some cases it is found a spurious response
      // of all 0xff's is received. Ignore such responses for now
      if (response->cmd == 0xff) {
        zxlogf(TRACE, "ot-radio: response received has cmd = 0xff, continuing\n");
        continue;
      }

      cmd_sent = ~cmd_sent;
      if (response->cmd != cmd_sent) {
        zxlogf(ERROR, "ot-radio: response received has cmd(%u) != cmd_sent(%u)\n", response->cmd,
               cmd_sent);
        return BL_ERR_RESP_CMD_MISMATCH;
      }

      if (response->length != exp_resp_size) {
        zxlogf(ERROR, "ot-radio: response received has length(%hu) != expected length(%zu)\n",
               response->length, exp_resp_size);
        return BL_ERR_RESP_LENGTH_MISMATCH;
      }

      uint32_t expected_crc = BlModeCrc32(kSpiPacketCrc32InitValue, &response->length,
                                          exp_resp_size - sizeof(response->crc32));
      if (response->crc32 != expected_crc) {
        zxlogf(ERROR, "ot-radio: response received has crc(%u) != expected crc(%u)\n",
               response->crc32, expected_crc);
        return BL_ERR_RESP_CRC_MISMATCH;
      }

      break;
    }
  }
  return BL_RET_SUCCESS;
}

bool OtRadioDeviceBootloader::FirmwareAlreadyUpToDateCRC(const std::vector<uint8_t> &fw_bytes) {
#ifdef INTERNAL_ACCESS
  return (VerifyUpload(fw_bytes) == 0);
#else
  return true;
#endif
}

zx_status_t OtRadioDeviceBootloader::PutRcpInBootloader() {
  zx_status_t status = ZX_OK;

  zxlogf(TRACE, "ot-radio : putting rcp in bootloader\n");

  status = dev_handle_->gpio_[OT_RADIO_BOOTLOADER_PIN].Write(0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: gpio write failed\n");
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(50)));

  status = dev_handle_->gpio_[OT_RADIO_RESET_PIN].Write(0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: gpio write failed\n");
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(50)));

  status = dev_handle_->gpio_[OT_RADIO_RESET_PIN].Write(1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: gpio write failed\n");
    return status;
  }

  // Note - give some time before releasing bootloader pin otherwise RCP
  // doesn't go into bootloader mode. This is because after reset, code
  // starts executing on RCP which checks for BOOTLOADER pin status, if
  // the BOOTLOADER pin is deasserted by that time, the code will proceed
  // assuming normal operation.
  // This also gives time for the bootloader to be up and ready to respond
  zx::nanosleep(zx::deadline_after(zx::msec(400)));

  status = dev_handle_->gpio_[OT_RADIO_BOOTLOADER_PIN].Write(1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: gpio write failed\n");
    return status;
  }

  zxlogf(TRACE, "ot-radio : device has been put in bootloader mode\n");

  return status;
}

OtRadioBlResult OtRadioDeviceBootloader::GetBootloaderVersion(std::string &bl_version) {
  OtRadioBlResult result = BL_RET_SUCCESS;

  // Now will attempt to get bootloader version
  NlSpiBlBasicCmd get_version;
  BlPrepareCmd(&get_version, sizeof(get_version), BL_SPI_CMD_GET_VERSION);

  // Send get_bootloader_version command
  result = SendSpiCmdAndGetResponse(reinterpret_cast<uint8_t *>(&get_version), sizeof(get_version),
                                    sizeof(NlSpiBlGetVersionResponse));
  if (result != BL_RET_SUCCESS) {
    zxlogf(ERROR,
           "ot-radio: Error in sending get-bootloader version command and getting response\n");
    return result;
  }

  NlSpiBlGetVersionResponse *response =
      reinterpret_cast<NlSpiBlGetVersionResponse *>(&dev_handle_->spi_rx_buffer_[0]);

  // Ensure that final byte in version is null (just in case response was buggy)
  response->version[kNlBootloaderVersionMaxLength - 1] = '\0';

  // Copy the version received
  bl_version.assign(reinterpret_cast<char *>(response->version));

  return result;
}

zx_status_t OtRadioDeviceBootloader::GetFirmwareBytes(std::vector<uint8_t> *fw_bytes) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  size_t size;

  zx_status_t load_fw_status =
      load_firmware(dev_handle_->parent(), "ot-ncp-app-release.bin", &vmo, &size);

  if (load_fw_status == ZX_OK) {
    zxlogf(TRACE, "ot-radio: load_firmware succeeded\n");
    fw_bytes->resize(size);
    zx_status_t vmo_read_status = zx_vmo_read(vmo, &(fw_bytes->front()), 0, size);

    if (vmo_read_status != ZX_OK) {
      zxlogf(ERROR, "ot-radio: failed to read vmo : %d\n", vmo_read_status);
      return vmo_read_status;
    }
  } else {
    zxlogf(ERROR, "ot-radio: load_firmware failed with error : %d\n", load_fw_status);
    return load_fw_status;
  }

  return ZX_OK;
}

OtRadioBlResult OtRadioDeviceBootloader::SendFlashEraseCmd(uint32_t address, uint32_t length) {
  NlSpiBlCmdFlashErase erase_cmd;
  erase_cmd.address = address;
  erase_cmd.length = length;
  BlPrepareCmd(&(erase_cmd.cmd), sizeof(erase_cmd), BL_SPI_CMD_FLASH_ERASE);

  OtRadioBlResult result;
  result = SendSpiCmdAndGetResponse(reinterpret_cast<uint8_t *>(&erase_cmd), sizeof(erase_cmd),
                                    sizeof(NlSpiBlBasicResponse));
  if (result != BL_RET_SUCCESS) {
    zxlogf(ERROR, "ot-radio: Error in sending flash-erase command and getting response\n");
  }

  return result;
}

OtRadioBlResult OtRadioDeviceBootloader::UploadFirmware(const std::vector<uint8_t> &fw_bytes) {
  unsigned long bytes_left = fw_bytes.size();
  unsigned retry_count = 0;
  const unsigned max_retry_count = 10;
  const uint8_t *current_ptr = reinterpret_cast<const uint8_t *>(&fw_bytes[0]);
  uint32_t address = kFwStartAddr;
  while ((bytes_left > 0) && (retry_count < max_retry_count)) {
    NlSpiBlCmdFlashWrite write_cmd;
    size_t bytes_to_write = sizeof(write_cmd.data);
    if (bytes_to_write > bytes_left) {
      bytes_to_write = bytes_left;
    }

    // Update various fields of write_cmd like address and data
    write_cmd.address = address;
    memcpy(write_cmd.data, current_ptr, bytes_to_write);

    // The actual size of spi command (can be less than sizeof(write_cmd))
    size_t spi_cmd_size = sizeof(write_cmd.cmd) + sizeof(write_cmd.address) + bytes_to_write;

    // Now prepare the cmd since rest of the fields are updated. As crc can be
    // calculated now
    BlPrepareCmd(&write_cmd.cmd, spi_cmd_size, BL_SPI_CMD_FLASH_WRITE);
    zxlogf(TRACE, "ot-radio: writing flash @ 0x%08x\n", address);

    OtRadioBlResult result;
    result = SendSpiCmdAndGetResponse(reinterpret_cast<uint8_t *>(&write_cmd), spi_cmd_size,
                                      sizeof(NlSpiBlBasicResponse));
    if (result != BL_RET_SUCCESS) {
      zxlogf(ERROR, "ot-radio: Sending flash write command got invalid response\n");
      return result;
    }

    // Response is stored in dev_handle_->spi_rx_buffer_ so read that
    NlSpiBlBasicResponse *basic_response =
        reinterpret_cast<NlSpiBlBasicResponse *>(&dev_handle_->spi_rx_buffer_[0]);

    if (basic_response->status != BL_ERROR_NONE) {
      zxlogf(ERROR, "ot-radio: Flash write error @ 0x%x, status=%d\n", address,
             basic_response->status);
      retry_count++;
      if (retry_count == max_retry_count) {
        return BL_ERR_WR_CMD_FAILED;
      } else {
        zxlogf(ERROR, "ot-radio: will retry %d more times\n", max_retry_count - retry_count);
        continue;
      }
    } else {
      retry_count = 0;
      // advance state
      address += bytes_to_write;
      current_ptr += bytes_to_write;
      bytes_left -= bytes_to_write;
    }
  }

  return BL_RET_SUCCESS;
}

// Verify the uploaded firmware by sending verify command to bootloader
// First compute the crc for entire firmware bytes, and then send the crc to
// bootloader to verify on it's end
OtRadioBlResult OtRadioDeviceBootloader::VerifyUpload(const std::vector<uint8_t> &fw_bytes) {
  NlSpiCmdFlashVerify verify_cmd;
  verify_cmd.address = kFwStartAddr;
  verify_cmd.length = fw_bytes.size();
  verify_cmd.crc32_check = BlModeCrc32(kSpiPacketCrc32InitValue, &fw_bytes[0], fw_bytes.size());
  BlPrepareCmd(&verify_cmd.cmd, sizeof(verify_cmd), BL_SPI_CMD_FLASH_VERIFY);
  zxlogf(TRACE, "ot-radio: Verifying crc32 of flashed image\n");

  OtRadioBlResult result;
  result = SendSpiCmdAndGetResponse(reinterpret_cast<uint8_t *>(&verify_cmd), sizeof(verify_cmd),
                                    sizeof(NlSpiBlBasicResponse));
  if (result != BL_RET_SUCCESS) {
    zxlogf(ERROR, "ot-radio: error in sending flash verify cmd and getting a response\n");
    return result;
  }

  NlSpiBlBasicResponse *basic_response =
      reinterpret_cast<NlSpiBlBasicResponse *>(&dev_handle_->spi_rx_buffer_[0]);

  if (basic_response->status != BL_ERROR_NONE) {
    zxlogf(ERROR, "ot-radio: Verification failed\n");
    return BL_ERR_VERIFICATION_FAILED;
  }

  return BL_RET_SUCCESS;
}

OtRadioBlResult OtRadioDeviceBootloader::UploadAndCheckFirmware(
    const std::vector<uint8_t> &fw_bytes) {
  OtRadioBlResult status;

  status = SendFlashEraseCmd(kFwStartAddr, fw_bytes.size());
  if (status != BL_RET_SUCCESS) {
    zxlogf(ERROR, "ot-radio: Flash Erase Command failed\n");
    return status;
  }

  status = UploadFirmware(fw_bytes);
  if (status != BL_RET_SUCCESS) {
    zxlogf(ERROR, "ot-radio: Upload Firmware command failed\n");
    return status;
  }

  status = VerifyUpload(fw_bytes);
  if (status != BL_RET_SUCCESS) {
    zxlogf(ERROR, "ot-radio: Verify Upload failed\n");
    return status;
  }

  return BL_RET_SUCCESS;
}

OtRadioBlResult OtRadioDeviceBootloader::UpdateRadioFirmware() {
#ifndef INTERNAL_ACCESS
  assert(0 && "Should not reach here without internal access");
#endif

  bl_zx_status = ZX_OK;

  if (GetNewFirmwareVersion().size() == 0) {
    // Invalid version string indicates invalid firmware
    zxlogf(ERROR, "ot-radio: The new firmware is invalid\n");
    return BL_ERR_INVALID_FW_VERSION;
  }

  std::vector<uint8_t> fw_bytes;
  zx_status_t status;
  if ((status = GetFirmwareBytes(&fw_bytes)) != ZX_OK) {
    zxlogf(ERROR, "ot-radio: GetFirmwareBytes failed with status : %d\n", status);
    bl_zx_status = status;
    return BL_ERR_GET_FW_BYTES_FAILED;
  }

  status = PutRcpInBootloader();
  if (status != ZX_OK) {
    bl_zx_status = status;
    // Attempt a Reset to try and clear up GPIO state:
    if ((status = dev_handle_->Reset()) != ZX_OK) {
      zxlogf(ERROR, "ot-radio: subsequent Reset call also failed, status: %d\n", status);
    }
    return BL_ERR_PUT_RCP_BLMODE_FAIL;
  }

  OtRadioBlResult result;
  result = UploadAndCheckFirmware(fw_bytes);
  // If result indicates an error, we should still reset the device first
  // before returning the error. So don't return early here.

  status = dev_handle_->Reset();
  if (status != ZX_OK) {
    bl_zx_status = status;
    return BL_ERR_RESET_FAILED;
  }

  return result;
}

}  // namespace ot
