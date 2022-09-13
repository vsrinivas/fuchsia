// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_MESON_MHU_COMMON_H_
#define SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_MESON_MHU_COMMON_H_

constexpr uint8_t kMboxMax = 4;
constexpr uint8_t kMhuIrq32 = 2;
constexpr uint8_t kMhuIrqMaxNumDef = 32;
constexpr uint32_t kMboxFifoSize = 0x80;
constexpr uint8_t kMboxUserLen = 96;
constexpr uint8_t kMboxHeadSize = 0x1c;
constexpr uint8_t kMboxResevSize = 0x4;
constexpr uint8_t kMboxUserCmdLen = 4;
constexpr uint8_t kAsyncCmd = 1;
constexpr uint32_t kSizeMask = 0x1FF;
constexpr uint8_t kMboxIrqShift = 32;
constexpr uint8_t kMailboxAocpu = 1;
constexpr uint8_t kMailboxDsp = 2;

#define BIT(pos) (1 << pos)
#define MBOX_DATA_SIZE (kMboxFifoSize - kMboxHeadSize - kMboxResevSize)
#define MBOX_USER_MAX_LEN (MBOX_DATA_SIZE + kMboxUserCmdLen)
#define SYNC_SHIFT(val) (val << 25)
#define SIZE_SHIFT(val) ((val & kSizeMask) << 16)
#define CMD_SHIFT(val) (val & UINT32_MAX)
#define IRQ_REV_BIT(mbox) (1 << (mbox * 2))
#define IRQ_SENDACK_BIT(mbox) (1 << (mbox * 2 + 1))
#define PAYLOAD_OFFSET(chan) (0x80 * (chan))
#define CTL_OFFSET(chan) (chan * 0x4)
#define IRQ_CLR_OFFSET(x) (0x20 + (x << 2))
#define IRQ_STS_OFFSET(x) (0x30 + (x << 2))
#define IRQ_CLR_OFFSETL(x) (0x40 + (x << 3))
#define IRQ_STS_OFFSETL(x) (0x80 + (x << 3))
#define IRQ_CLR_OFFSETH(x) (0x44 + (x << 3))
#define IRQ_STS_OFFSETH(x) (0x84 + (x << 3))
#define SYNC_CMD_TAG BIT(26)
#define ASYNC_CMD_TAG BIT(25)

#endif  // SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_MESON_MHU_COMMON_H_
