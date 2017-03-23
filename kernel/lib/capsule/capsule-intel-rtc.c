// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//

#include <lib/capsule.h>

#include <arch/mp.h>
#include <arch/x86.h>
#include <platform.h>

#if WITH_LIB_CONSOLE
#include <string.h>
#include <lib/console.h>
#endif

// We use the Intel RTC CMOS bank 2 that holds 128 bytes.
// Since the storage is small we only define one slot so
// only index 0 is available.

// Format in bytes is:
//   00  tag
//   01  size
//   02  tag (inverted)
//   03  size (inverted)
//   04  payload
//   ..
//   ..
//   x0  last byte of payload (at size + 4)
//   x1  sum (mod 256) of payload
//

#define RTC_BANK_MAX 128u
#define RTC_CAPSULE_MAX  (RTC_BANK_MAX - 5u)

#define CMOS_SPIN_LOCK_START                          \
    spin_lock_saved_state_t irqstate;                 \
    spin_lock_irqsave(&cmos_spinlock, irqstate)

#define CMOS_SPIN_LOCK_END                            \
    spin_unlock_irqrestore(&cmos_spinlock, irqstate)

static spin_lock_t cmos_spinlock = SPIN_LOCK_INITIAL_VALUE;


static void cmos_write_bank2(uint8_t pos, uint8_t value) {
    if (pos >= RTC_BANK_MAX)
        return;
    outp(0x72, pos);
    outp(0x73, value);
}

static uint8_t cmos_read_bank2(uint8_t pos) {
    if (pos >= RTC_BANK_MAX)
        return 0;
    outp(0x72, pos);
    return inp(0x73);
}

static uint32_t cmos_dump_bank2(int32_t index, uint8_t* buf, uint32_t size) {
    if (index != 0)
        return 0;
    if (size < RTC_BANK_MAX)
        return 0;

    CMOS_SPIN_LOCK_START;

    for (uint8_t c = 0u; c < RTC_BANK_MAX; c++) {
        buf[c] = cmos_read_bank2(c);
    }

    CMOS_SPIN_LOCK_END;

    return RTC_BANK_MAX;
}

int32_t capsule_store(uint8_t tag, void* capsule, uint32_t size) {
    if (!capsule && (size == 0))
        return RTC_CAPSULE_MAX;

    if (size > RTC_CAPSULE_MAX)
        return CAP_ERR_SIZE;

    CMOS_SPIN_LOCK_START;

    uint32_t utag = tag;

    cmos_write_bank2(0u, utag);
    cmos_write_bank2(1u, size);
    cmos_write_bank2(2u, ~utag);
    cmos_write_bank2(3u, ~size);

    uint8_t sum = 0u;
    uint8_t c = 4u;
    for (uint8_t i = 0u; i < size; i++, c++) {
        uint8_t val = ((uint8_t*)capsule)[i];
        cmos_write_bank2(c, val);
        sum += val;
    }

    cmos_write_bank2(c + 1, sum);

    CMOS_SPIN_LOCK_END;
    return 0;
}

int32_t capsule_fetch(uint8_t tag, void* capsule, uint32_t size) {
    CMOS_SPIN_LOCK_START;

    uint8_t otag = cmos_read_bank2(0u);
    uint8_t count = cmos_read_bank2(1u);
    uint8_t itag = ~cmos_read_bank2(2u);
    uint8_t icount = ~cmos_read_bank2(3u);

    int32_t status = 0;
    if ((count != icount) || (otag != itag))
        status = CAP_ERR_BAD_HEADER;
    if (tag != otag)
        status = CAP_ERR_NOT_FOUND;
    if (size < count)
        status = CAP_ERR_SIZE;

    if (status != 0)
        goto end;

    uint8_t sum = 0u;
    uint8_t c = 4u;
    for (uint8_t i = 0u; i < count; i++, c++) {
        uint8_t val = cmos_read_bank2(c);
        ((uint8_t*)capsule)[i] = val;
        sum += val;
    }

    uint8_t esum = cmos_read_bank2(c + 1);
    status = (esum == sum) ? count : CAP_ERR_CHECKSUM;

end:
    CMOS_SPIN_LOCK_END;
    return status;
}

#if WITH_LIB_CONSOLE

static void dump_cmd(uint32_t index) {
    uint8_t capsule[RTC_BANK_MAX];
    uint32_t count = cmos_dump_bank2(index, capsule, sizeof(capsule));
    if (count == 0) {
        printf("capsule slot not available\n");
    } else {
        hexdump(capsule, sizeof(capsule));
    }
}

static int cmd_capsule(int argc, const cmd_args *argv, uint32_t flags) {
    if (argc == 2) {
        dump_cmd(argv[1].u);
    } else {
        printf("usage:\n%s <slot>\n", argv[0].str);
    }
    return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("capsule", "dump capsule", &cmd_capsule)
STATIC_COMMAND_END(capsule);

#endif  // WITH_LIB_CONSOLE
