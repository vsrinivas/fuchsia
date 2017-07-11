// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <magenta/syscalls.h>
#include "tpm.h"

#define TPM_LOCALITY_BASE(locality) ((uintptr_t)(tpm_base) + ((uintptr_t)(locality) << 12))
#define TPM_ACCESS(locality) (volatile uint8_t *)(TPM_LOCALITY_BASE(locality))
#define TPM_INT_ENABLE(locality) (volatile uint32_t *)(TPM_LOCALITY_BASE(locality) + 0x08)
#define TPM_INT_VECTOR(locality) (volatile uint8_t *)(TPM_LOCALITY_BASE(locality) + 0x0c)
#define TPM_INT_STATUS(locality) (volatile uint32_t *)(TPM_LOCALITY_BASE(locality) + 0x10)
#define TPM_INTF_CAP(locality) (volatile uint32_t *)(TPM_LOCALITY_BASE(locality) + 0x14)
#define TPM_STS(locality) (volatile uint32_t *)(TPM_LOCALITY_BASE(locality) + 0x18)
#define TPM_DATA_FIFO(locality) (volatile uint8_t *)(TPM_LOCALITY_BASE(locality) + 0x24)
#define TPM_INTERFACE_ID(locality) (volatile uint32_t *)(TPM_LOCALITY_BASE(locality) + 0x30)
#define TPM_XDATA_FIFO(locality) (volatile uint32_t *)(TPM_LOCALITY_BASE(locality) + 0x80)
#define TPM_DID_VID(locality) (volatile uint32_t *)(TPM_LOCALITY_BASE(locality) + 0xf00)
#define TPM_RID(locality) (volatile uint32_t *)(TPM_LOCALITY_BASE(locality) + 0xf04)

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

// Timeouts (in ns)
#define TIMEOUT_A 750000  //  750 ms
#define TIMEOUT_B 2000000 // 2000 ms
#define TIMEOUT_C 200000  //  200 ms
#define TIMEOUT_D 30000   //   30 ms

mx_status_t tpm_set_irq(enum locality loc, uint8_t vector) {
    if (vector < 1 || vector > 15) {
        return MX_ERR_OUT_OF_RANGE;
    }
    *TPM_INT_VECTOR(loc) = vector;
    // Enable TPM interrupts (top-level mask bit)
    *TPM_INT_ENABLE(loc) |= TPM_INT_ENABLE_GLOBAL_ENABLE;
    // TODO(teisenbe): get rid of this, need to discover supported signal modes
    // This is not doable yet, since our interrupt syscalls do not allow
    // configuring signaling modes yet.
    *TPM_INT_ENABLE(loc) |= TPM_INT_ENABLE_RISING_EDGE;
    return MX_OK;
}

mx_status_t tpm_is_supported(enum locality loc) {
    const uint32_t iface_id = *TPM_INTERFACE_ID(loc);
    switch (iface_id & TPM_INTERFACE_ID_TYPE_MASK) {
        case TPM_INTERFACE_ID_TYPE_FIFO_1_3: {
            const uint32_t iface_ver = *TPM_INTF_CAP(loc) & TPM_INTF_CAP_IFACE_VER_MASK;
            if (iface_ver == TPM_INTF_CAP_IFACE_VER_1_2) {
                return MX_OK;
            }
            return MX_ERR_NOT_SUPPORTED;
        }
        case TPM_INTERFACE_ID_TYPE_FIFO_2_0:
        case TPM_INTERFACE_ID_TYPE_CRB:
        default:
            return MX_ERR_NOT_SUPPORTED;
    }
}

mx_status_t tpm_request_use(enum locality loc) {
    uint8_t val;
    if (!((val = *TPM_ACCESS(loc)) & TPM_ACCESS_REG_VALID)) {
        return MX_ERR_BAD_STATE;
    }

    if (val & TPM_ACCESS_REQUEST_USE) {
        return MX_ERR_UNAVAILABLE;
    }

    if (val & TPM_ACCESS_ACTIVE_LOCALITY) {
        // We're already the active locality
        return MX_ERR_BAD_STATE;
    }

    mx_status_t status = tpm_enable_irq_type(loc, IRQ_LOCALITY_CHANGE);
    if (status != MX_OK) {
        return status;
    }
    *TPM_ACCESS(loc) = TPM_ACCESS_REQUEST_USE;

    return MX_OK;
}

mx_status_t tpm_wait_for_locality(enum locality loc) {
    uint8_t val;
    if (!((val = *TPM_ACCESS(loc)) & TPM_ACCESS_REG_VALID)) {
        return MX_ERR_BAD_STATE;
    }
    if (val & TPM_ACCESS_ACTIVE_LOCALITY) {
        return MX_OK;
    }
    if (!(val & TPM_ACCESS_REQUEST_USE)) {
        return MX_ERR_BAD_STATE;
    }
    // We assume we're the only one using the TPM, so we need to wait at most
    // TIMEOUT_A
    mx_nanosleep(mx_deadline_after(TIMEOUT_A));

    if (!((val = *TPM_ACCESS(loc)) & TPM_ACCESS_REG_VALID)) {
        return MX_ERR_BAD_STATE;
    }
    if (val & TPM_ACCESS_ACTIVE_LOCALITY) {
        return MX_OK;
    }
    if (val & TPM_ACCESS_REQUEST_USE) {
        return MX_ERR_TIMED_OUT;
    }
    return MX_ERR_BAD_STATE;
}

mx_status_t tpm_enable_irq_type(enum locality loc, enum irq_type type) {
    if (!(*TPM_INTF_CAP(loc) & type)) {
        return MX_ERR_NOT_SUPPORTED;
    }
    *TPM_INT_ENABLE(loc) |= (uint32_t)type;
    return MX_OK;
}

mx_status_t tpm_disable_irq_type(enum locality loc, enum irq_type type) {
    if (!(*TPM_INTF_CAP(loc) & type)) {
        return MX_ERR_NOT_SUPPORTED;
    }
    *TPM_INT_ENABLE(loc) &= ~(uint32_t)type;
    return MX_OK;
}

static mx_status_t get_status_field(enum locality loc, uint32_t *val) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt) {
            mx_nanosleep(mx_deadline_after(TIMEOUT_A));
        }

        uint32_t status = *TPM_STS(loc);
        if (status & TPM_STS_VALID) {
            *val = status;
            return MX_OK;
        }
    }

    return MX_ERR_TIMED_OUT;
}

static mx_status_t get_burst_count(enum locality loc, uint16_t *val) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt) {
            mx_nanosleep(mx_deadline_after(TIMEOUT_A));
        }

        uint32_t status = *TPM_STS(loc);
        uint16_t burst = TPM_STS_EXTRACT_BURST_COUNT(status);
        if (burst > 0) {
            *val = burst;
            return MX_OK;
        }
    }

    return MX_ERR_TIMED_OUT;
}

// Returns the true/false value of the the STS.EXPECT bit, or < 0 on error
static mx_status_t get_status_expect(enum locality loc, bool* expect) {
    uint32_t status_field;
    mx_status_t status = get_status_field(loc, &status_field);
    if (status != MX_OK) {
        return status;
    }
    *expect = !!(status_field & TPM_STS_EXPECT);
    return MX_OK;
}

// Returns the true/false value of the the STS.DATA_AVAIL bit, or < 0 on error
static mx_status_t get_status_data_avail(enum locality loc, bool* data_avail) {
    uint32_t status_field;
    mx_status_t status = get_status_field(loc, &status_field);
    if (status != MX_OK) {
        return status;
    }
    *data_avail = !!(status_field & TPM_STS_DATA_AVAIL);
    return MX_OK;
}

static mx_status_t wait_for_data_avail(enum locality loc) {
    // TODO(teisenbe): Add a timeout to this?
    while (1) {
        bool data_avail = false;
        mx_status_t st = get_status_data_avail(loc, &data_avail);
        if (st < 0) {
            return st;
        }
        if (data_avail) {
            return MX_OK;
        }

        st = mx_interrupt_wait(irq_handle);
        if (st < 0) {
            return st;
        }
        // Clear triggered interrupt flags
        if (*TPM_INT_STATUS(loc) & IRQ_DATA_AVAIL) {
            *TPM_INT_STATUS(loc) = IRQ_DATA_AVAIL;
        }
        if (*TPM_INT_STATUS(loc) & IRQ_LOCALITY_CHANGE) {
            *TPM_INT_STATUS(loc) = IRQ_LOCALITY_CHANGE;
            // If locality changed, whatever operation we're in the middle of
            // is no longer valid..
            mx_interrupt_complete(irq_handle);
            return MX_ERR_INTERNAL;
        }
        mx_interrupt_complete(irq_handle);
    }
}

static void abort_command(enum locality loc) {
    *TPM_STS(loc) = TPM_STS_CMD_RDY;
}

// Returns the true/false value of the the ACCESS.ACTIVE bit, or < 0 on error
static mx_status_t get_active_locality(enum locality loc, bool* active) {
    uint8_t val;
    if (!((val = *TPM_ACCESS(loc)) & TPM_ACCESS_REG_VALID)) {
        return MX_ERR_BAD_STATE;
    }
    *active = !!(val & TPM_ACCESS_ACTIVE_LOCALITY);
    return MX_OK;
}

static mx_status_t check_expected_state(
        mx_status_t status, bool actual, bool expected) {
    if (status < 0) {
        return status;
    }
    if (actual != expected) {
        return MX_ERR_BAD_STATE;
    }
    return MX_OK;
}

mx_status_t tpm_send_cmd(enum locality loc, uint8_t* cmd, size_t len) {
    bool active = false;
    mx_status_t st = get_active_locality(loc, &active);
    st = check_expected_state(st, active, true);
    if (st < 0) {
        return st;
    }

    if (!(*TPM_STS(loc) & TPM_STS_CMD_RDY)) {
        return MX_ERR_UNAVAILABLE;
    }

    // This procedure is described in section 5.5.2.2.1 of the TCG PC Client
    // Platform TPM profile spec (family 2.0, which also describes 1.2)
    *TPM_STS(loc) = TPM_STS_CMD_RDY;

    size_t bytes_sent = 0;

    // Write the command to the FIFO, while respecting flow control
    while (bytes_sent < len) {
        uint16_t burst_count;
        st = get_burst_count(loc, &burst_count);
        if (st != MX_OK) {
            abort_command(loc);
            return st;
        }

        // Write up to len - 1 bytes, since we should watch the EXPECT bit
        // transition on the final byte
        while (burst_count > 0 && bytes_sent < len - 1) {
            *TPM_DATA_FIFO(loc) = cmd[bytes_sent];
            ++bytes_sent;
            --burst_count;
        }

        if (burst_count > 0 && bytes_sent == len - 1) {
            bool expect = false;
            // Watch the EXPECT bit as we write the last byte, it should
            // transition.
            st = get_status_expect(loc, &expect);
            st = check_expected_state(st, expect, true);
            if (st < 0) {
                abort_command(loc);
                return st;
            }

            *TPM_DATA_FIFO(loc) = cmd[bytes_sent];
            ++bytes_sent;

            st = get_status_expect(loc,&expect);
            st = check_expected_state(st, expect, false);
            if (st < 0) {
                abort_command(loc);
                return st;
            }
        }
    }

    // Run the command
    *TPM_STS(loc) = TPM_STS_TPM_GO;
    return MX_OK;
}

ssize_t tpm_recv_resp(enum locality loc, uint8_t* resp, size_t max_len) {
    bool active = false;
    mx_status_t st = get_active_locality(loc, &active);
    st = check_expected_state(st, active, true);
    if (st < 0) {
        abort_command(loc);
        return st;
    }

    // This procedure is described in section 5.5.2.2.2 of the TCG PC Client
    // Platform TPM profile spec (family 2.0, which also describes 1.2)

    // Wait for data to be available
    st = wait_for_data_avail(loc);
    if (st != MX_OK) {
        abort_command(loc);
        return st;
    }

    bool more_data = true;
    size_t bytes_recvd = 0;
    while (more_data) {
        uint16_t burst_count;
        st = get_burst_count(loc, &burst_count);
        if (st != MX_OK) {
            abort_command(loc);
            return st;
        }
        // We can read up to burst_count, but there may be less data than that

        for (size_t i = 0; i < burst_count; ++i) {
            resp[bytes_recvd] = *TPM_DATA_FIFO(loc);
            ++bytes_recvd;

            // See if there is any more data to read
            bool data_avail = false;
            st = get_status_data_avail(loc, &data_avail);
            if (st < 0) {
                abort_command(loc);
                return st;
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
    }

    // Either abort a response if we filled our buffer, or acknowledge that
    // we've finished receiving the data. (Transitions 30 and 37 in Table 22
    // (State Transition Table)).
    *TPM_STS(loc) = TPM_STS_CMD_RDY;

    return bytes_recvd;
}
