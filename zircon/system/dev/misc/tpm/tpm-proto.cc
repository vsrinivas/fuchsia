// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zircon/syscalls.h>
#include <lib/zx/time.h>
#include "tpm.h"

// TPM_ACCESS bitmasks
#define TPM_ACCESS_REG_VALID       0x80
#define TPM_ACCESS_ACTIVE_LOCALITY 0x20
#define TPM_ACCESS_BEEN_SEIZED     0x10
#define TPM_ACCESS_SEIZE           0x08
#define TPM_ACCESS_PENDING_REQ     0x04
#define TPM_ACCESS_REQUEST_USE     0x02
#define TPM_ACCESS_ESTABLISHMENT   0x01

// TPM_INTF_CAP bitmasks
#define TPM_INTF_CAP_IFACE_VER_MASK 0x70000000
#define TPM_INTF_CAP_IFACE_VER_1_3  0x20000000
#define TPM_INTF_CAP_IFACE_VER_1_2  0x00000000

// TPM_STS bitmasks
#define TPM_STS_FAMILY              0x0c000000
#define TPM_STS_RESET_ESTABLISHMENT 0x02000000
#define TPM_STS_CMD_CANCEL          0x01000000
#define TPM_STS_BURST_COUNT         0x00ffff00
#define TPM_STS_VALID               0x00000080
#define TPM_STS_CMD_RDY             0x00000040
#define TPM_STS_TPM_GO              0x00000020
#define TPM_STS_DATA_AVAIL          0x00000010
#define TPM_STS_EXPECT              0x00000008
#define TPM_STS_SELF_TEST_DONE      0x00000004
#define TPM_STS_RESPONSE_RETRY      0x00000002
#define TPM_STS_EXTRACT_BURST_COUNT(sts) (((sts) & TPM_STS_BURST_COUNT) >> 8)
#define TPM_STS_EXTRACT_FAMILY(sts) (((sts) & TPM_STS_FAMILY) >> 26)

// TPM_INT_ENABLE bitmasks
#define TPM_INT_ENABLE_GLOBAL_ENABLE 0x80000000
#define TPM_INT_ENABLE_HIGH_LEVEL    (0 << 3)
#define TPM_INT_ENABLE_LOW_LEVEL     (1 << 3)
#define TPM_INT_ENABLE_RISING_EDGE   (2 << 3)
#define TPM_INT_ENABLE_FALLING_EDGE  (3 << 3)

// TPM_INTERFACE_ID bitmasks
#define TPM_INTERFACE_ID_TYPE_MASK     0xf
#define TPM_INTERFACE_ID_TYPE_FIFO_2_0 0x0
#define TPM_INTERFACE_ID_TYPE_CRB      0x1
#define TPM_INTERFACE_ID_TYPE_FIFO_1_3 0xf

namespace {

constexpr zx::duration TIMEOUT_A = zx::msec(750);

constexpr zx::duration kWaitForProgressDelay = zx::msec(2);

} // namespace

namespace tpm {

constexpr size_t kNumRegisterTries = 3;

static zx_status_t GetAccessField(HardwareInterface* iface, Locality loc,
                                  uint8_t* val) {
    for (size_t attempt = 0; attempt < kNumRegisterTries; ++attempt) {
        if (attempt) {
            zx::nanosleep(zx::deadline_after(TIMEOUT_A));
        }

        uint8_t access;
        zx_status_t status = iface->ReadAccess(loc, &access);
        if (status != ZX_OK) {
            return status;
        }
        if (access & TPM_ACCESS_REG_VALID) {
            *val = access;
            return ZX_OK;
        }
    }

    return ZX_ERR_TIMED_OUT;
}

zx_status_t Device::RequestLocalityLocked(Locality loc) {
    uint8_t val;
    zx_status_t status = GetAccessField(iface_.get(), loc, &val);
    if (status != ZX_OK) {
        return status;
    }
    if (!(val & TPM_ACCESS_REG_VALID)) {
        return ZX_ERR_BAD_STATE;
    }

    if (val & TPM_ACCESS_REQUEST_USE) {
        return ZX_ERR_UNAVAILABLE;
    }

    if (val & TPM_ACCESS_ACTIVE_LOCALITY) {
        // We're already the active locality
        return ZX_ERR_BAD_STATE;
    }

    return iface_->WriteAccess(loc, TPM_ACCESS_REQUEST_USE);
}

zx_status_t Device::ReleaseLocalityLocked(Locality loc) {
    uint8_t val;
    zx_status_t status = GetAccessField(iface_.get(), loc, &val);
    if (status != ZX_OK) {
        return status;
    }
    if (!(val & TPM_ACCESS_REG_VALID)) {
        return ZX_ERR_BAD_STATE;
    }

    if (val & TPM_ACCESS_REQUEST_USE) {
        return ZX_ERR_BAD_STATE;
    }

    if (!(val & TPM_ACCESS_ACTIVE_LOCALITY)) {
        // We're not the active locality
        return ZX_ERR_BAD_STATE;
    }

    // Writing this bit triggers the release.
    return iface_->WriteAccess(loc, TPM_ACCESS_ACTIVE_LOCALITY);
}

zx_status_t Device::WaitForLocalityLocked(Locality loc) {
    uint8_t val;
    zx_status_t status = GetAccessField(iface_.get(), loc, &val);
    if (status != ZX_OK) {
        return status;
    }
    if (val & TPM_ACCESS_ACTIVE_LOCALITY) {
        return ZX_OK;
    }
    if (!(val & TPM_ACCESS_REQUEST_USE)) {
        return ZX_ERR_BAD_STATE;
    }
    // We assume we're the only one using the TPM, so we need to wait at most
    // TIMEOUT_A
    zx::nanosleep(zx::deadline_after(TIMEOUT_A));

    status = GetAccessField(iface_.get(), loc, &val);
    if (status != ZX_OK) {
        return status;
    }
    if (val & TPM_ACCESS_ACTIVE_LOCALITY) {
        return ZX_OK;
    }
    if (val & TPM_ACCESS_REQUEST_USE) {
        return ZX_ERR_TIMED_OUT;
    }
    return ZX_ERR_BAD_STATE;
}

static zx_status_t GetStatusField(HardwareInterface* iface, Locality loc,
                                  uint32_t* val) {
    for (size_t attempt = 0; attempt < kNumRegisterTries; ++attempt) {
        if (attempt) {
            zx::nanosleep(zx::deadline_after(TIMEOUT_A));
        }

        uint32_t sts;
        zx_status_t status = iface->ReadStatus(loc, &sts);
        if (status != ZX_OK) {
            return status;
        }
        if (sts & TPM_STS_VALID) {
            *val = sts;
            return ZX_OK;
        }
    }

    return ZX_ERR_TIMED_OUT;
}

static zx_status_t GetBurstCount(HardwareInterface* iface, Locality loc,
                                 uint16_t* val) {
    for (size_t attempt = 0; attempt < kNumRegisterTries; ++attempt) {
        if (attempt) {
            zx::nanosleep(zx::deadline_after(TIMEOUT_A));
        }

        uint32_t sts;
        zx_status_t status = iface->ReadStatus(loc, &sts);
        if (status != ZX_OK) {
            return status;
        }
        uint16_t burst = TPM_STS_EXTRACT_BURST_COUNT(sts);
        if (burst > 0) {
            *val = burst;
            return ZX_OK;
        }
    }

    return ZX_ERR_TIMED_OUT;
}

// Returns the true/false value of the the STS.EXPECT bit, or < 0 on error
static zx_status_t GetStatusExpect(HardwareInterface* iface, Locality loc,
                                   bool* expect) {
    uint32_t status_field;
    zx_status_t status = GetStatusField(iface, loc, &status_field);
    if (status != ZX_OK) {
        return status;
    }
    *expect = !!(status_field & TPM_STS_EXPECT);
    return ZX_OK;
}

// Returns the true/false value of the the STS.DATA_AVAIL bit, or < 0 on error
static zx_status_t GetStatusDataAvail(HardwareInterface* iface, Locality loc,
                                      bool* data_avail) {
    uint32_t status_field;
    zx_status_t status = GetStatusField(iface, loc, &status_field);
    if (status != ZX_OK) {
        return status;
    }
    *data_avail = !!(status_field & TPM_STS_DATA_AVAIL);
    return ZX_OK;
}

static zx_status_t WaitForDataAvail(HardwareInterface* iface, Locality loc) {
    // TODO(teisenbe): Add a timeout to this?
    while (1) {
        bool data_avail = false;
        zx_status_t status = GetStatusDataAvail(iface, loc, &data_avail);
        if (status != ZX_OK) {
            return status;
        }
        if (data_avail) {
            return ZX_OK;
        }

        zx::nanosleep(zx::deadline_after(kWaitForProgressDelay));
    }
}

static zx_status_t AbortCommand(HardwareInterface* iface, Locality loc) {
    return iface->WriteStatus(loc, TPM_STS_CMD_RDY);
}

// Returns the true/false value of the the ACCESS.ACTIVE bit, or < 0 on error
static zx_status_t GetActiveLocality(HardwareInterface* iface, Locality loc,
                                     bool* active) {
    uint8_t val;
    zx_status_t status = GetAccessField(iface, loc, &val);
    if (status != ZX_OK) {
        return status;
    }
    *active = !!(val & TPM_ACCESS_ACTIVE_LOCALITY);
    return ZX_OK;
}

static zx_status_t CheckExpectedState(zx_status_t status, bool actual, bool expected) {
    if (status < 0) {
        return status;
    }
    if (actual != expected) {
        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
}

zx_status_t Device::SendCmdLocked(Locality loc, const uint8_t* cmd, size_t len) {
    if (len <= 1) {
        return ZX_ERR_INVALID_ARGS;
    }

    bool active = false;
    zx_status_t status = GetActiveLocality(iface_.get(), loc, &active);
    status = CheckExpectedState(status, active, true);
    if (status < 0) {
        return status;
    }

    // This procedure is described in section 5.5.2.2.1 of the TCG PC Client
    // Platform TPM profile spec (family 2.0, which also describes 1.2)
    status = iface_->WriteStatus(loc, TPM_STS_CMD_RDY);
    if (status != ZX_OK) {
        return status;
    }

    size_t bytes_sent = 0;

    // Write the command to the FIFO, while respecting flow control
    while (bytes_sent < len) {
        uint16_t burst_count;
        status = GetBurstCount(iface_.get(), loc, &burst_count);
        if (status != ZX_OK) {
            AbortCommand(iface_.get(), loc);
            return status;
        }
        if (burst_count == 0) {
            AbortCommand(iface_.get(), loc);
            return ZX_ERR_IO;
        }

        // Write up to len - 1 bytes, since we should watch the EXPECT bit
        // transition on the final byte
        uint16_t to_write = burst_count;
        if (to_write > len - 1) {
            to_write = static_cast<uint16_t>(len - 1);
        }

        status = iface_->WriteDataFifo(loc, &cmd[bytes_sent], to_write);
        if (status != ZX_OK) {
            AbortCommand(iface_.get(), loc);
            return status;
        }
        bytes_sent += to_write;
        burst_count = static_cast<uint16_t>(burst_count - to_write);

        if (burst_count > 0 && bytes_sent == len - 1) {
            bool expect = false;
            // Watch the EXPECT bit as we write the last byte, it should
            // transition.
            status = GetStatusExpect(iface_.get(), loc, &expect);
            status = CheckExpectedState(status, expect, true);
            if (status < 0) {
                AbortCommand(iface_.get(), loc);
                return status;
            }

            status = iface_->WriteDataFifo(loc, &cmd[bytes_sent], 1);
            if (status != ZX_OK) {
                AbortCommand(iface_.get(), loc);
                return status;
            }
            ++bytes_sent;

            status = GetStatusExpect(iface_.get(), loc, &expect);
            status = CheckExpectedState(status, expect, false);
            if (status < 0) {
                AbortCommand(iface_.get(), loc);
                return status;
            }
        }
    }

    // Run the command
    status = iface_->WriteStatus(loc, TPM_STS_TPM_GO);
    if (status != ZX_OK) {
        AbortCommand(iface_.get(), loc);
        return status;
    }
    return ZX_OK;
}

zx_status_t Device::RecvRespLocked(Locality loc, uint8_t* resp, size_t max_len, size_t* actual) {
    bool active = false;
    zx_status_t status = GetActiveLocality(iface_.get(), loc, &active);
    status = CheckExpectedState(status, active, true);
    if (status != ZX_OK) {
        AbortCommand(iface_.get(), loc);
        return status;
    }

    // This procedure is described in section 5.5.2.2.2 of the TCG PC Client
    // Platform TPM profile spec (family 2.0, which also describes 1.2)

    // Wait for data to be available
    status = WaitForDataAvail(iface_.get(), loc);
    if (status != ZX_OK) {
        AbortCommand(iface_.get(), loc);
        return status;
    }

    bool more_data = true;
    size_t bytes_recvd = 0;
    while (more_data) {
        zxlogf(TRACE, "Reading response, %zu bytes read\n", bytes_recvd);
        uint16_t burst_count;
        status = GetBurstCount(iface_.get(), loc, &burst_count);
        if (status != ZX_OK) {
            AbortCommand(iface_.get(), loc);
            return status;
        }

        size_t to_read = burst_count;
        size_t remaining = max_len - bytes_recvd;
        if (to_read > remaining) {
            to_read = remaining;
        }
        status = iface_->ReadDataFifo(loc, &resp[bytes_recvd], to_read);
        if (status != ZX_OK) {
            AbortCommand(iface_.get(), loc);
            return status;
        }
        bytes_recvd += to_read;

        // See if there is any more data to read
        bool data_avail = false;
        status = GetStatusDataAvail(iface_.get(), loc, &data_avail);
        if (status < 0) {
            AbortCommand(iface_.get(), loc);
            return status;
        } else if (!data_avail) {
            more_data = false;
            break;
        }

        // If we have filled the buffer and there is more data, exit
        // the loop so we will send a CMD_RDY (which doubles as an abort).
        if (bytes_recvd >= max_len) {
            more_data = false;
        }
    }

    // Either abort a response if we filled our buffer, or acknowledge that
    // we've finished receiving the data. (Transitions 30 and 37 in Table 22
    // (State Transition Table)).
    AbortCommand(iface_.get(), loc);

    *actual = bytes_recvd;
    return ZX_OK;
}

} // namespace tpm
