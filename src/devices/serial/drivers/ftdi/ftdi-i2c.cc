// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftdi-i2c.h"

#include <fuchsia/hardware/ftdi/llcpp/fidl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>

#include <vector>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/metadata/i2c.h>

#include "ftdi.h"

namespace ftdi_mpsse {

zx_status_t FtdiI2c::Enable() {
  zx_status_t status;
  status = mpsse_.Init();
  if (status != ZX_OK) {
    return status;
  }
  if (!mpsse_.IsValid()) {
    zxlogf(ERROR, "ftdi_i2c: mpsse is invalid!\n");
    return ZX_ERR_INTERNAL;
  }

  status = mpsse_.Sync();
  if (status != ZX_OK) {
    zxlogf(ERROR, "ftdi_i2c: mpsse failed to sync %d\n", status);
    return status;
  }
  status = mpsse_.FlushGpio();
  if (status != ZX_OK) {
    zxlogf(ERROR, "ftdi_i2c: mpsse failed flush GPIO\n");
    return status;
  }

  status = mpsse_.SetClock(false, true, 100000);
  if (status != ZX_OK) {
    return status;
  }

  // Enable drive-zero mode -- this means sending 0 to GPIO drives outputs low
  // and sending 1 drives them with tri-state. This matches the I2C protocol
  // and lets multiple devices share the bus.
  uint8_t buf[3] = {kFtdiCommandDriveZeroMode, 0x07, 0x00};
  status = mpsse_.Write(buf, sizeof(buf));
  if (status != ZX_OK) {
    return status;
  }

  std::vector<uint8_t> buffer(6);
  size_t bytes_written;
  status = WriteIdleToBuf(0, &buffer, &bytes_written);
  if (status != ZX_OK) {
    return status;
  }
  status = mpsse_.Write(buffer.data(), buffer.size());

  DdkMakeVisible();
  return ZX_OK;
}

zx_status_t FtdiI2c::Bind() {
  zx_status_t status = DdkAdd("ftdi-i2c", DEVICE_ADD_INVISIBLE);
  if (status != ZX_OK) {
    return status;
  }

  std::vector<i2c_channel_t> i2c_channels(i2c_devices_.size());
  for (size_t i = 0; i < i2c_devices_.size(); i++) {
    i2c_channel_t& chan = i2c_channels[i];
    I2cDevice& dev = i2c_devices_[i];
    chan.bus_id = 0;
    chan.address = static_cast<uint16_t>(dev.address);
    chan.vid = dev.vid;
    chan.pid = dev.pid;
    chan.did = dev.did;
  }
  status = DdkAddMetadata(DEVICE_METADATA_I2C_CHANNELS, i2c_channels.data(),
                          i2c_channels.size() * sizeof(i2c_channel_t));
  if (status != ZX_OK) {
    DdkRemoveDeprecated();
    return status;
  }

  auto f = [](void* arg) -> int { return reinterpret_cast<FtdiI2c*>(arg)->Enable(); };

  int rc = thrd_create_with_name(&enable_thread_, f, this, "ftdi-i2c-enable-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

// This adds the command to set SCL and SDA high into buffer. It must be called
// at least once for initial setup.
zx_status_t FtdiI2c::WriteIdleToBuf(size_t index, std::vector<uint8_t>* buffer,
                                    size_t* bytes_written) {
  mpsse_.SetGpio(pin_layout_.scl, Mpsse::Direction::OUT, Mpsse::Level::HIGH);
  mpsse_.SetGpio(pin_layout_.sda_out, Mpsse::Direction::OUT, Mpsse::Level::HIGH);
  mpsse_.SetGpio(pin_layout_.sda_in, Mpsse::Direction::IN, Mpsse::Level::LOW);
  mpsse_.GpioWriteCommandToBuffer(index, buffer, bytes_written);
  return ZX_OK;
}

// This adds the command to write one byte over I2C into the buffer.
void FtdiI2c::WriteI2CByteWriteToBuf(size_t index, uint8_t byte, std::vector<uint8_t>* buffer,
                                     size_t* bytes_written) {
  size_t new_index = index;
  (*buffer)[new_index++] = kI2cWriteCommandByte1;
  (*buffer)[new_index++] = kI2cWriteCommandByte2;
  (*buffer)[new_index++] = kI2cWriteCommandByte3;
  (*buffer)[new_index++] = byte;

  mpsse_.SetGpio(pin_layout_.scl, Mpsse::Direction::OUT, Mpsse::Level::LOW);
  mpsse_.SetGpio(pin_layout_.sda_out, Mpsse::Direction::OUT, Mpsse::Level::HIGH);
  size_t temp_written = 0;
  mpsse_.GpioWriteCommandToBuffer(new_index, buffer, &temp_written);
  new_index += temp_written;

  // Read bit for ACK/NAK.
  (*buffer)[new_index++] = kI2cReadAckCommandByte1;
  (*buffer)[new_index++] = kI2cReadAckCommandByte2;
  *bytes_written = new_index - index;
}

void FtdiI2c::WriteI2CByteReadToBuf(size_t index, bool final_byte, std::vector<uint8_t>* buffer,
                                    size_t* bytes_written) {
  size_t temp_written = 0;
  size_t new_index = index;
  if (final_byte) {
    memcpy(buffer->data() + new_index, kI2cReadFinalByteCommand, sizeof(kI2cReadFinalByteCommand));
    new_index += sizeof(kI2cReadFinalByteCommand);
  } else {
    memcpy(buffer->data() + new_index, kI2cReadOneByteCommand, sizeof(kI2cReadOneByteCommand));
    new_index += sizeof(kI2cReadOneByteCommand);
  }
  mpsse_.SetGpio(pin_layout_.scl, Mpsse::Direction::OUT, Mpsse::Level::LOW);
  mpsse_.SetGpio(pin_layout_.sda_out, Mpsse::Direction::OUT, Mpsse::Level::HIGH);
  mpsse_.GpioWriteCommandToBuffer(new_index, buffer, &temp_written);
  new_index += temp_written;
  *bytes_written = new_index - index;
}

void FtdiI2c::DdkUnbindNew(ddk::UnbindTxn txn) {
  thrd_join(enable_thread_, NULL);
  txn.Reply();
}

zx_status_t FtdiI2c::Transact(uint8_t bus_address, std::vector<uint8_t> write_data,
                              std::vector<uint8_t>* read_data) {
  size_t transaction_size;
  size_t expected_reads = 0;
  size_t read_size = (read_data == nullptr) ? 0 : read_data->size();
  bool is_read = (read_size != 0);
  if (!is_read) {
    transaction_size =
        kI2cNumCommandBytesPerFullWrite + (kI2cNumCommandBytesPerWriteByte * write_data.size());
  } else {
    transaction_size = kI2cNumCommandBytesPerFullReadWrite +
                       (kI2cNumCommandBytesPerWriteByte * write_data.size()) +
                       (kI2cNumCommandBytesPerReadByte * read_size);
  }

  std::vector<uint8_t> transaction(transaction_size);
  size_t transaction_index = 0;
  size_t bytes_written = 0;

  zx_status_t status = WriteIdleToBuf(transaction_index, &transaction, &bytes_written);
  if (status != ZX_OK) {
    return status;
  }
  transaction_index += bytes_written;

  status = WriteTransactionStartToBuf(transaction_index, &transaction, &bytes_written);
  if (status != ZX_OK) {
    return status;
  }
  transaction_index += bytes_written;

  auto it = write_data.begin();
  it = write_data.insert(it, static_cast<uint8_t>(bus_address << 1));

  for (size_t i = 0; i < write_data.size(); i++) {
    WriteI2CByteWriteToBuf(transaction_index, write_data[i], &transaction, &bytes_written);
    transaction_index += bytes_written;
    expected_reads++;
  }

  status = WriteTransactionEndToBuf(transaction_index, &transaction, &bytes_written);
  if (status != ZX_OK) {
    return status;
  }
  transaction_index += bytes_written;

  if (is_read) {
    zx_status_t status = WriteIdleToBuf(transaction_index, &transaction, &bytes_written);
    if (status != ZX_OK) {
      return status;
    }
    transaction_index += bytes_written;

    status = WriteTransactionStartToBuf(transaction_index, &transaction, &bytes_written);
    if (status != ZX_OK) {
      return status;
    }
    transaction_index += bytes_written;

    WriteI2CByteWriteToBuf(transaction_index, static_cast<uint8_t>(bus_address << 1 | 0x1),
                           &transaction, &bytes_written);
    transaction_index += bytes_written;
    expected_reads++;

    // Send the read commands.
    for (size_t i = 0; i < read_size; i++) {
      WriteI2CByteReadToBuf(transaction_index, (i == (read_size - 1)), &transaction,
                            &bytes_written);
      transaction_index += bytes_written;
      expected_reads++;
    }

    status = WriteTransactionEndToBuf(transaction_index, &transaction, &bytes_written);
    if (status != ZX_OK) {
      return status;
    }
    transaction_index += bytes_written;
  }

  // Ask for response immediately
  transaction[transaction_index++] = kI2cCommandFinishTransaction;

  if (transaction_index != transaction.size()) {
    return ZX_ERR_INTERNAL;
  }

  status = mpsse_.Write(transaction.data(), transaction.size());
  if (status != ZX_OK) {
    return status;
  }

  std::vector<uint8_t> response(expected_reads);
  status = mpsse_.Read(response.data(), response.size());
  if (status != ZX_OK) {
    return status;
  }

  // Check each response byte to see if its an ACK (zero) or NACK (non-zero).
  for (size_t i = 0; i < response.size() - read_size; i++) {
    if ((response[i] & 0x1) != 0) {
      zxlogf(INFO, "Ftdi-i2c: Recieved NACK on byte %ld (data=%d)\n", i, response[i]);
      return ZX_ERR_INTERNAL;
    }
  }

  // Copy the read information.
  if (read_size) {
    memcpy(read_data->data(), response.data() + (response.size() - read_size), read_size);
  }

  return ZX_OK;
}

zx_status_t FtdiI2c::Ping(uint8_t bus_address) {
  std::vector<uint8_t> data(1);
  data[0] = 0x00;
  return Transact(bus_address, data, nullptr);
}

// This adds the command to transition SCL from high to low.
zx_status_t FtdiI2c::WriteTransactionStartToBuf(size_t index, std::vector<uint8_t>* buffer,
                                                size_t* bytes_written) {
  size_t sub_written = 0;
  *bytes_written = 0;

  mpsse_.SetGpio(pin_layout_.scl, Mpsse::Direction::OUT, Mpsse::Level::HIGH);
  mpsse_.SetGpio(pin_layout_.sda_out, Mpsse::Direction::OUT, Mpsse::Level::LOW);
  mpsse_.GpioWriteCommandToBuffer(index + *bytes_written, buffer, &sub_written);
  *bytes_written += sub_written;

  mpsse_.SetGpio(pin_layout_.scl, Mpsse::Direction::OUT, Mpsse::Level::LOW);
  mpsse_.SetGpio(pin_layout_.sda_out, Mpsse::Direction::OUT, Mpsse::Level::LOW);
  mpsse_.GpioWriteCommandToBuffer(index + *bytes_written, buffer, &sub_written);
  *bytes_written += sub_written;

  return ZX_OK;
}

zx_status_t FtdiI2c::WriteTransactionEndToBuf(size_t index, std::vector<uint8_t>* buffer,
                                              size_t* bytes_written) {
  size_t sub_written = 0;
  *bytes_written = 0;

  mpsse_.SetGpio(pin_layout_.scl, Mpsse::Direction::OUT, Mpsse::Level::LOW);
  mpsse_.SetGpio(pin_layout_.sda_out, Mpsse::Direction::OUT, Mpsse::Level::LOW);
  mpsse_.GpioWriteCommandToBuffer(index + *bytes_written, buffer, &sub_written);
  *bytes_written += sub_written;

  mpsse_.SetGpio(pin_layout_.scl, Mpsse::Direction::OUT, Mpsse::Level::HIGH);
  mpsse_.SetGpio(pin_layout_.sda_out, Mpsse::Direction::OUT, Mpsse::Level::LOW);
  mpsse_.GpioWriteCommandToBuffer(index + *bytes_written, buffer, &sub_written);
  *bytes_written += sub_written;

  mpsse_.SetGpio(pin_layout_.scl, Mpsse::Direction::OUT, Mpsse::Level::HIGH);
  mpsse_.SetGpio(pin_layout_.sda_out, Mpsse::Direction::OUT, Mpsse::Level::HIGH);
  mpsse_.GpioWriteCommandToBuffer(index + *bytes_written, buffer, &sub_written);
  *bytes_written += sub_written;

  return ZX_OK;
}

zx_status_t FtdiI2c::I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list,
                                     size_t op_count) {
  zx_status_t status;
  std::vector<uint8_t> write_data(kFtdiI2cMaxTransferSize);
  std::vector<uint8_t> read_data(kFtdiI2cMaxTransferSize);
  size_t total_read_bytes = 0;
  size_t total_write_bytes = 0;
  size_t last_stopped_op = 0;
  for (size_t i = 0; i < op_count; i++) {
    if (op_list[i].is_read) {
      total_read_bytes += op_list[i].data_size;
    } else {
      size_t copy_amt = op_list[i].data_size;
      uint8_t* data_buffer = reinterpret_cast<uint8_t*>(op_list[i].data_buffer);
      size_t data_buffer_index = 0;
      while (copy_amt--) {
        if (total_write_bytes == kFtdiI2cMaxTransferSize) {
          return ZX_ERR_INTERNAL;
        }
        write_data[total_write_bytes++] = data_buffer[data_buffer_index++];
      }
    }

    if (op_list[i].stop) {
      write_data.resize(total_write_bytes);
      read_data.resize(total_read_bytes);
      status = Transact(static_cast<uint8_t>(op_list[i].address), write_data, &read_data);
      if (status != ZX_OK) {
        zxlogf(ERROR, "I2c transact failed with %d\n", status);
        return status;
      }

      if (total_read_bytes > 0) {
        size_t read_back_index = 0;
        for (size_t j = last_stopped_op + 1; j <= i; j++) {
          if (op_list[j].is_read) {
            memcpy(op_list[j].data_buffer, read_data.data() + read_back_index,
                   op_list[j].data_size);
            read_back_index += op_list[j].data_size;
          }
        }
      }

      // Reset the write_data for the next transaction.
      write_data.resize(kFtdiI2cMaxTransferSize);
      read_data.resize(kFtdiI2cMaxTransferSize);
      total_write_bytes = 0;

      // Reset the read data for the next transaction.
      total_read_bytes = 0;

      last_stopped_op = i;
    }
  }

  return ZX_OK;
}

zx_status_t FtdiI2c::Create(zx_device_t* device,
                            const ::llcpp::fuchsia::hardware::ftdi::I2cBusLayout* layout,
                            const ::llcpp::fuchsia::hardware::ftdi::I2cDevice* i2c_dev) {
  // TODO(dgilhooley: Support i2c on different sets of pins and then remove this check.
  if (layout->scl != 0 && layout->sda_out != 1 && layout->sda_in != 2) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  ftdi_mpsse::FtdiI2c::I2cLayout i2c_layout;
  i2c_layout.scl = layout->scl;
  i2c_layout.sda_out = layout->sda_out;
  i2c_layout.sda_in = layout->sda_in;

  std::vector<ftdi_mpsse::FtdiI2c::I2cDevice> i2c_devices(1);
  i2c_devices[0].address = i2c_dev->address;
  i2c_devices[0].vid = i2c_dev->vid;
  i2c_devices[0].pid = i2c_dev->pid;
  i2c_devices[0].did = i2c_dev->did;

  auto dev = std::make_unique<ftdi_mpsse::FtdiI2c>(device, i2c_layout, i2c_devices);
  zx_status_t status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    dev.release();
  }

  return ZX_OK;
}

}  // namespace ftdi_mpsse
