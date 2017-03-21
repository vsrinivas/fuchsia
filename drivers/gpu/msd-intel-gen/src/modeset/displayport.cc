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

} // namespace

bool I2cOverDpAux::SendDpAuxMsg(const DpAuxMessage* request, DpAuxMessage* reply)
{
    uint32_t data_reg = registers::DdiAuxData::GetOffset(ddi_number_);

    // Write the outgoing message to the hardware.
    DASSERT(request->size <= DpAuxMessage::kMaxTotalSize);
    for (uint32_t offset = 0; offset < request->size; offset += 4) {
        reg_io_->Write32(data_reg + offset, request->GetPackedWord(offset));
    }

    auto status = registers::DdiAuxControl::Get(ddi_number_).FromValue(0);
    status.sync_pulse_count().set(31);
    status.message_size().set(request->size);
    // Setting the send_busy bit initiates the transaction.
    status.send_busy().set(1);
    status.WriteTo(reg_io_);

    // Poll for the reply message.
    const int kNumTries = 10000;
    for (int tries = 0; tries < kNumTries; ++tries) {
        auto status = registers::DdiAuxControl::Get(ddi_number_).ReadFrom(reg_io_);
        if (!status.send_busy().get()) {
            // TODO(MA-150): Test for handling of timeout errors
            if (status.timeout().get())
                return DRETF(false, "DP aux: Got timeout error");
            reply->size = status.message_size().get();
            if (reply->size > DpAuxMessage::kMaxTotalSize)
                return DRETF(false, "DP aux: Invalid reply size");
            // Read the reply message from the hardware.
            for (uint32_t offset = 0; offset < reply->size; offset += 4) {
                reply->SetFromPackedWord(offset, reg_io_->Read32(data_reg + offset));
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    return DRETF(false, "DP aux: No reply after %d tries", kNumTries);
}

bool I2cOverDpAux::SendDpAuxMsgWithRetry(const DpAuxMessage* request, DpAuxMessage* reply)
{
    // If the DisplayPort sink device isn't ready to handle an Aux message,
    // it can return an AUX_DEFER reply, which means we should retry the
    // request.  The spec says "A DP Source device is required to retry at
    // least seven times upon receiving AUX_DEFER before giving up the AUX
    // transaction", from section 2.7.7.1.5.6.1 in v1.3.  (AUX_DEFER
    // replies were in earlier versions, but v1.3 clarified the number of
    // retries required.)
    const int kNumTries = 8;

    for (int tries = 0; tries < kNumTries; ++tries) {
        if (!SendDpAuxMsg(request, reply)) {
            // We do not retry if sending the raw message fails with a
            // reason such as a timeout.
            return false;
        }

        // Read the header byte.  This contains a 4-bit status field and 4
        // bits of zero padding.  The status field is in the upper bits
        // because it is sent across the wire first and because DP Aux uses
        // big endian bit ordering.
        if (reply->size < 1)
            return DRETF(false, "DP aux: Unexpected zero-size reply (header byte missing)");
        uint8_t header_byte = reply->data[0];
        uint8_t padding = header_byte & 0xf;
        uint8_t status = header_byte >> 4;
        // Sanity check: The padding should be zero.  If it's not, we
        // shouldn't return an error, in case this space gets used for some
        // later extension to the protocol.  But report it, in case this
        // indicates some problem.
        if (padding)
            magma::log(magma::LOG_WARNING,
                       "DP aux: Reply header padding is non-zero (header byte: 0x%x)", header_byte);

        switch (status) {
            case DisplayPort::DP_REPLY_AUX_ACK:
                // The AUX_ACK implies that we got an I2C ACK too.
                return true;
            case DisplayPort::DP_REPLY_AUX_DEFER:
                // Go around the loop again to retry.
                continue;
            case DisplayPort::DP_REPLY_AUX_NACK:
                return DRETF(false, "DP aux: Reply was not an ack (got AUX_NACK)");
            case DisplayPort::DP_REPLY_I2C_NACK:
                return DRETF(false, "DP aux: Reply was not an ack (got I2C_NACK)");
            case DisplayPort::DP_REPLY_I2C_DEFER:
                // TODO(MA-150): Implement handling of I2C_DEFER.
                return DRETF(false, "DP aux: Received I2C_DEFER (not implemented)");
            default:
                // We got a reply that is not defined by the DisplayPort spec.
                return DRETF(false, "DP aux: Unrecognized reply (header byte: 0x%x)", header_byte);
        }
    }
    return DRETF(false, "DP aux: Received too many AUX DEFERs (%d)", kNumTries);
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
    if (!SetDpAuxHeader(&msg, addr, /* is_read= */ true, size_in) ||
        !SendDpAuxMsgWithRetry(&msg, &reply)) {
        return false;
    }
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
    if (!SendDpAuxMsgWithRetry(&msg, &reply))
        return false;
    // TODO(MA-150): Handle the case where the hardware did a short write,
    // for which we could send the remaining bytes.
    if (reply.size != 1)
        return DRETF(false, "DP aux write: Unexpected reply size");
    return true;
}

bool DisplayPort::FetchEdidData(RegisterIo* reg_io, uint32_t ddi_number, uint8_t* buf,
                                uint32_t size)
{
    I2cOverDpAux i2c(reg_io, ddi_number);

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
    uint32_t logged_count = 0;

    for (uint32_t ddi_number = 0; ddi_number < registers::Ddi::kDdiCount; ++ddi_number) {
        // Read enough just to test that we got the correct header.
        uint8_t buf[32];
        if (!FetchEdidData(reg_io, ddi_number, buf, sizeof(buf)))
            continue;

        static const uint8_t kEdidHeader[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
        if (memcmp(buf, kEdidHeader, sizeof(kEdidHeader)) != 0) {
            magma::log(magma::LOG_WARNING, "DDI %d: EDID: Read EDID data, but got bad header",
                       ddi_number);
        } else {
            magma::log(magma::LOG_INFO,
                       "DDI %d: EDID: Read EDID data successfully, with correct header",
                       ddi_number);
        }
        ++logged_count;
    }

    if (logged_count == 0)
        magma::log(magma::LOG_INFO, "EDID: Read EDID data for 0 DDIs");
}
