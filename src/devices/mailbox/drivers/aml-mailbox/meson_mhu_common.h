// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_MESON_MHU_COMMON_H_
#define SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_MESON_MHU_COMMON_H_

#define __MBX_COMPOSE_MSG(mod, func) (((mod) << 10) | ((func)&0x3FF))
#define MBX_TEST_DEMO __MBX_COMPOSE_MSG(MBX_SYSTEM, CMD_MBX_TEST)
#define MBX_CMD_RPCUINT_TESTA __MBX_COMPOSE_MSG(MBX_SYSTEM, CMD_RPCUINT_TESTA)
#define MBX_SYSTEM 0x0
#define CMD_MBX_TEST 0x6
#define CMD_RPCUINT_TESTA 0x61

#define BIT(pos) (1 << (pos))
#define false 0
#define true 1
#define MBOX_MAX 10
#define MHU_IRQ32 2
#define RPCUINT_SIZE 64
#define MHUIRQ_MAXNUM_DEF 32
#define MBOX_FIFO_SIZE 0x80
#define CHANNEL_FIFO_MAX MBOX_MAX
#define MBOX_USER_LEN 96
#define MBOX_HEAD_SIZE 0x1c
#define MBOX_RESEV_SIZE 0x4
#define MBOX_USER_CMD_LEN 4
#define MBOX_DATA_SIZE (MBOX_FIFO_SIZE - MBOX_HEAD_SIZE - MBOX_RESEV_SIZE)
#define MBOX_USER_MAX_LEN (MBOX_DATA_SIZE + MBOX_USER_CMD_LEN)

#define ASYNC_CMD 1
#define SYNC_SHIFT(val) ((val) << 25)
#define SIZE_MASK 0x1FF
#define SIZE_SHIFT(val) (((val)&SIZE_MASK) << 16)
#define CMD_MASK 0xFFFF
#define CMD_SHIFT(val) (((val)&CMD_MASK) << 0)

#define IRQ_REV_BIT(mbox) (1ULL << ((mbox)*2))
#define IRQ_SENDACK_BIT(mbox) (1ULL << ((mbox)*2 + 1))
#define PAYLOAD_OFFSET(chan) (0x80 * (chan))
#define CTL_OFFSET(chan) ((chan)*0x4)
#define IRQ_CLR_OFFSET(x) (0x20 + ((x) << 2))
#define IRQ_STS_OFFSET(x) (0x30 + ((x) << 2))
#define IRQ_CLR_OFFSETL(x) (0x40 + ((x) << 3))
#define IRQ_STS_OFFSETL(x) (0x80 + ((x) << 3))
#define IRQ_CLR_OFFSETH(x) (0x44 + ((x) << 3))
#define IRQ_STS_OFFSETH(x) (0x84 + ((x) << 3))

#define MBOX_IRQMASK 0xffffffff
#define MBOX_IRQSHIFT 32
#define MBOX_USER_SIZE 0x80
#define MAILBOX_AOCPU 1
#define MAILBOX_DSP 2
#define SYNC_CMD_TAG BIT(26)
#define ASYNC_CMD_TAG BIT(25)
#define MBSIZE_SHIFT 16
#define MBSIZE_MASK 0x1ff
#define MBCMD_MASK 0xffff

#endif  // SRC_DEVICES_MAILBOX_DRIVERS_AML_MAILBOX_MESON_MHU_COMMON_H_
