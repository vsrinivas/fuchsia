// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modeset/displayport.h"
#include "magma_util/macros.h"
#include "register_io.h"
#include "registers.h"
#include <algorithm>
#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <thread>

namespace {

// Fill out the header of a DisplayPort Aux message.  For write operations,
// |body_size| is the size of the body of the message to send.  For read
// operations, |body_size| is the size of our receive buffer.
bool SetDpAuxHeader(DpAuxMessage* msg, uint32_t addr, bool is_read, uint32_t body_size)
{
    if (body_size > DpAuxMessage::kMaxBodySize)
        return DRETF(false, "DP aux: Message too large");
    // For now, we don't handle messages with empty bodies.  (However, they
    // can be used for checking whether there is an I2C device at a given
    // address.)
    if (body_size == 0)
        return DRETF(false, "DP aux: Empty message not supported");
    // For now, we only handle 8-bit addresses.  Also, we only handle
    // I2C-over-Aux messages, not native Aux messages.
    if (addr >= 0x100)
        return DRETF(false, "DP aux: Large address not supported");
    uint32_t dp_cmd = is_read;
    msg->data[0] = dp_cmd << 4;
    msg->data[1] = 0;
    msg->data[2] = addr;
    // For writes, the size of the message will be encoded twice:
    //  * The msg->size field contains the total message size (header and
    //    body).
    //  * If the body of the message is non-empty, the header contains an
    //    extra field specifying the body size (in bytes minus 1).
    // For reads, the message to send is a header only.
    msg->size = 4;
    msg->data[3] = body_size - 1;
    return true;
}

// This implements sending I2C read and write requests over DisplayPort.
class I2cOverDpAux {
public:
    I2cOverDpAux(RegisterIo* reg_io) : reg_io_(reg_io) {}

    // Send an I2C read request.  If this fails to read the full
    // |size| bytes into |buf|, it returns false for failure.
    bool I2cRead(uint32_t addr, uint8_t* buf, uint32_t size);
    // Send an I2C write request.
    bool I2cWrite(uint32_t addr, const uint8_t* buf, uint32_t size);

private:
    // Send a DisplayPort Aux message and receive the synchronous reply
    // message.
    bool SendDpAuxMsg(const DpAuxMessage* request, DpAuxMessage* reply);
    // Read a single chunk, upto the DisplayPort Aux message size limit.
    bool I2cReadChunk(uint32_t addr, uint8_t* buf, uint32_t size_in, uint32_t* size_out);

    RegisterIo* reg_io_;
};

bool I2cOverDpAux::SendDpAuxMsg(const DpAuxMessage* request, DpAuxMessage* reply)
{
    // Write the outgoing message to the hardware.
    DASSERT(request->size <= DpAuxMessage::kMaxTotalSize);
    for (uint32_t offset = 0; offset < request->size; offset += 4) {
        reg_io_->Write32(registers::DDIAuxData::kOffset + offset, request->GetPackedWord(offset));
    }

    // Set kSendBusyBit to initiate the transaction.
    reg_io_->Write32(registers::DDIAuxControl::kOffset,
                     registers::DDIAuxControl::kSendBusyBit | registers::DDIAuxControl::kFlags |
                         (request->size << registers::DDIAuxControl::kMessageSizeShift));

    // Poll for the reply message.
    const int kNumTries = 10000;
    for (int tries = 0; tries < kNumTries; ++tries) {
        uint32_t status = reg_io_->Read32(registers::DDIAuxControl::kOffset);
        if ((status & registers::DDIAuxControl::kSendBusyBit) == 0) {
            // TODO(MA-150): Test for handling of timeout errors
            if (status & registers::DDIAuxControl::kTimeoutBit)
                return DRETF(false, "DP aux: Got timeout error\n");
            reply->size = (status >> registers::DDIAuxControl::kMessageSizeShift) &
                          registers::DDIAuxControl::kMessageSizeMask;
            if (reply->size > DpAuxMessage::kMaxTotalSize)
                return DRETF(false, "DP aux: Invalid reply size\n");
            // Read the reply message from the hardware.
            for (uint32_t offset = 0; offset < reply->size; offset += 4) {
                reply->SetFromPackedWord(offset,
                                         reg_io_->Read32(registers::DDIAuxData::kOffset + offset));
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    return DRETF(false, "DP aux: No reply after %d tries\n", kNumTries);
}

bool I2cOverDpAux::I2cRead(uint32_t addr, uint8_t* buf, uint32_t size)
{
    while (size > 0) {
        uint32_t chunk_size = std::min(size, uint32_t{DpAuxMessage::kMaxBodySize});
        uint32_t bytes_read = 0;
        if (!I2cReadChunk(addr, buf, chunk_size, &bytes_read))
            return false;
        if (bytes_read == 0) {
            // We failed to make progress on the last call.  To avoid the
            // risk of getting an infinite loop from that happening
            // continually, we return.
            return false;
        }
        DASSERT(size >= bytes_read);
        buf += bytes_read;
        size -= bytes_read;
    }
    return true;
}

bool I2cOverDpAux::I2cReadChunk(uint32_t addr, uint8_t* buf, uint32_t size_in, uint32_t* size_out)
{
    DpAuxMessage msg;
    DpAuxMessage reply;
    if (!SetDpAuxHeader(&msg, addr, /* is_read= */ true, size_in) || !SendDpAuxMsg(&msg, &reply)) {
        return false;
    }
    if (reply.size == 0)
        return DRETF(false, "DP aux read: Reply should contain 1 header byte");
    if (reply.data[0] != 0)
        return DRETF(false, "DP aux read: Reply was not an ack");
    uint32_t bytes_read = reply.size - 1;
    if (bytes_read > size_in)
        return DRETF(false, "DP aux read: Reply was larger than requested");
    DASSERT(bytes_read <= DpAuxMessage::kMaxBodySize);
    memcpy(buf, &reply.data[1], bytes_read);
    *size_out = bytes_read;
    return true;
}

// This does not support writes more than the message body size limit for
// DisplayPort Aux (16 bytes), since we haven't needed that yet.
bool I2cOverDpAux::I2cWrite(uint32_t addr, const uint8_t* buf, uint32_t size)
{
    DpAuxMessage msg;
    DpAuxMessage reply;
    if (!SetDpAuxHeader(&msg, addr, /* is_read= */ false, size))
        return false;
    memcpy(&msg.data[4], buf, size);
    msg.size = size + 4;
    if (!SendDpAuxMsg(&msg, &reply))
        return false;
    // TODO(MA-150): Handle the case where the hardware did a short write,
    // for which we could send the remaining bytes.
    if (reply.size != 1)
        return DRETF(false, "DP aux write: Unexpected reply size");
    if (reply.data[0] != 0)
        return DRETF(false, "DP aux write: Reply was not an ack");
    return true;
}

} // namespace

bool DisplayPort::FetchEdidData(RegisterIo* reg_io, uint8_t* buf, uint32_t size)
{
    I2cOverDpAux i2c(reg_io);

    // Seek to the start of the EDID data, in case the current seek
    // position is non-zero.
    uint8_t data_offset = 0;
    if (!i2c.I2cWrite(kDdcI2cAddress, &data_offset, 1))
        return false;

    // Read the data.
    return i2c.I2cRead(kDdcI2cAddress, buf, size);
}

// This function is some bare minimum functionality for allowing us to test
// FetchEdidData() by running the driver on real hardware and eyeballing
// the log output.  Once we can bring up a display, we won't need this
// function as it is now.
void DisplayPort::FetchAndCheckEdidData(RegisterIo* reg_io)
{
    // Read enough just to test that we got the correct header.
    uint8_t buf[32];
    if (!FetchEdidData(reg_io, buf, sizeof(buf))) {
        magma::log(magma::LOG_WARNING, "edid: FetchEdidData() failed\n");
        return;
    }
    static const uint8_t kEdidHeader[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
    if (memcmp(buf, kEdidHeader, sizeof(kEdidHeader)) != 0) {
        magma::log(magma::LOG_WARNING, "edid: got bad header\n");
        return;
    }
    magma::log(magma::LOG_INFO, "edid: read EDID data successfully, with correct header\n");
}
