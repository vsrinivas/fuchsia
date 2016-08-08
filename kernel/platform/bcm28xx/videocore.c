// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch.h>
#include <arch/arm64.h>
#include <arch/arm64/mmu.h>
#include <arch/ops.h>
#include <platform/bcm28xx.h>
#include <platform/videocore.h>

#define VC_MBOX_BUFF_SIZE 1024

/* Buffer for Videocore mailbox communications.  Needs to be 16 byte aligned
 *  but extending to 64 to ensure occupying full cache lines.
 */
volatile static uint8_t vc_mbox_buff[VC_MBOX_BUFF_SIZE] __ALIGNED(64);
volatile static fb_mbox_t fb_mbox __ALIGNED(64);

/* Take kernel aspace virt address and convert to Videocore bus address.
 *      Videocore physical address base varies based on if L2 is used.
 *      Bus addresses are 32-bit when used in this context
 */
static uint32_t vc_virt_to_bus(uintptr_t vaddr) {
    return (uint32_t)(vaddr - KERNEL_ASPACE_BASE + BCM_SDRAM_BUS_ADDR_BASE)&(0xffffffff)
}

/* Format message Videocore mailbox
  */
static uint32_t vc_mbox_message(uint32_t message, uint32_t channel) {

    if (*REG32(ARM0_MAILBOX_STATUS) & VCORE_MAILBOX_FULL) {
        printf("Mailbox full-ERR\n:");
        return -VCORE_ERR_MBOX_FULL;
    }

    ISB;
    DSB;
    *REG32(ARM0_MAILBOX_WRITE) = (message & 0xfffffff0) | (channel & 0x0000000f);
    ISB;
    DSB;

    uint32_t i = 0;
    while (*REG32(ARM0_MAILBOX_STATUS) & VCORE_MAILBOX_EMPTY) {
        i++;
        if (i >= VCORE_READ_ATTEMPTS) {
            printf("empty\n");
            return -VCORE_ERR_MBOX_TIMEOUT;
        }
    }

    return VCORE_SUCCESS;
}


uint32_t get_vcore_framebuffer(fb_mbox_t* fb_mbox) {

    uint32_t resp_addr,errcode;

    arch_clean_cache_range(fb_mbox, sizeof(fb_mbox_t));

    if (errcode = vc_mbox_message(vcore_virt_to_bus(fb_mbox),  VC_FB_CHANNEL))
            return errcode;

    resp_addr = *REG32(ARM0_MAILBOX_READ);

    arch_invalidate_cache_range(fb_mbox, sizeof(fb_mbox_t));

    return VCORE_SUCCESS;
}

uint32_t _get_vcore_single(uint32_t tag, uint32_t req_len, uint8_t * rsp, uint32_t rsp_len) {

    int i;

    uint32_t* word_buff = vc_mbox_buff;
    word_buff[0] = 8 + sizeof(tag) + sizeof(req_len) + sizeof(rsp_len) + rsp_len + 1;
    word_buff[1] = VCORE_TAG_REQUEST;
    word_buff[2] = tag;
    word_buff[3] = req_len;
    word_buff[4] = rsp_len;
    for (i = 5; i < 5 + rsp_len; i++) {
        word_buff[i] = 0;
    }

    word_buff[i] = VCORE_ENDTAG;

    arch_clean_cache_range(vc_mbox_buff, VC_MBOX_BUFF_SIZE);

    if (errcode = vc_mbox_message(vcore_virt_to_bus(word_buff),  ARM_TO_VC_CHANNEL))
        return errcode;

    resp_addr = *REG32(ARM0_MAILBOX_READ);

    arch_invalidate_cache_range(vc_mbox_buff, VC_MBOX_BUFF_SIZE);

    rsp = &word_buff[5];

    return VCORE_SUCCESS;
}
