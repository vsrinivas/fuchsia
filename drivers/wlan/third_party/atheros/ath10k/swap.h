/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SWAP_H_
#define _SWAP_H_

#define ATH10K_SWAP_CODE_SEG_BIN_LEN_MAX    (512 * 1024)
#define ATH10K_SWAP_CODE_SEG_MAGIC_BYTES_SZ 12
#define ATH10K_SWAP_CODE_SEG_NUM_MAX        16
/* Currently only one swap segment is supported */
#define ATH10K_SWAP_CODE_SEG_NUM_SUPPORTED  1

struct ath10k_fw_file;

struct ath10k_swap_code_seg_tlv {
    uint32_t address;
    uint32_t length;
    uint8_t data[0];
} __packed;

struct ath10k_swap_code_seg_tail {
    uint8_t magic_signature[ATH10K_SWAP_CODE_SEG_MAGIC_BYTES_SZ];
    uint32_t bmi_write_addr;
} __packed;

union ath10k_swap_code_seg_item {
    struct ath10k_swap_code_seg_tlv tlv;
    struct ath10k_swap_code_seg_tail tail;
} __packed;

struct ath10k_swap_code_seg_hw_info {
    /* Swap binary image size */
    uint32_t swap_size;
    uint32_t num_segs;

    /* Swap data size */
    uint32_t size;
    uint32_t size_log2;
    uint32_t bus_addr[ATH10K_SWAP_CODE_SEG_NUM_MAX];
    uint64_t reserved[ATH10K_SWAP_CODE_SEG_NUM_MAX];
} __packed;

struct ath10k_swap_code_seg_info {
    struct ath10k_swap_code_seg_hw_info seg_hw_info;
    void* virt_address[ATH10K_SWAP_CODE_SEG_NUM_SUPPORTED];
    uint32_t target_addr;
    dma_addr_t paddr[ATH10K_SWAP_CODE_SEG_NUM_SUPPORTED];
};

int ath10k_swap_code_seg_configure(struct ath10k* ar,
                                   const struct ath10k_fw_file* fw_file);
void ath10k_swap_code_seg_release(struct ath10k* ar,
                                  struct ath10k_fw_file* fw_file);
int ath10k_swap_code_seg_init(struct ath10k* ar,
                              struct ath10k_fw_file* fw_file);

#endif
