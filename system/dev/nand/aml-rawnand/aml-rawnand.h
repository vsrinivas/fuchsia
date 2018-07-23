// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

typedef enum raw_nand_addr_window {
    NANDREG_WINDOW = 0,
    CLOCKREG_WINDOW,
    ADDR_WINDOW_COUNT, // always last
} raw_nand_addr_window_t;

typedef struct {
    int ecc_strength;
    int user_mode;
    int rand_mode;
#define NAND_USE_BOUNCE_BUFFER 0x1
    int options;
    int bch_mode;
} aml_controller_t;

typedef struct {
    raw_nand_protocol_t raw_nand_proto;
    platform_device_protocol_t pdev;
    zx_device_t* zxdev;
    io_buffer_t mmio[ADDR_WINDOW_COUNT];
    thrd_t irq_thread;
    zx_handle_t irq_handle;
    bool enabled;
    aml_controller_t controller_params;
    uint32_t chip_select;
    int chip_delay;
    uint32_t writesize; /* NAND pagesize - bytes */
    uint32_t erasesize; /* size of erase block - bytes */
    uint32_t erasesize_pages;
    uint32_t oobsize; /* oob bytes per NAND page - bytes */
#define NAND_BUSWIDTH_16 0x00000002
    uint32_t bus_width;  /* 16bit or 8bit ? */
    uint64_t chipsize;   /* MiB */
    uint32_t page_shift; /* NAND page shift */
    completion_t req_completion;
    struct {
        uint64_t ecc_corrected;
        uint64_t failed;
    } stats;
    io_buffer_t data_buffer;
    io_buffer_t info_buffer;
    zx_handle_t bti_handle;
    void *info_buf, *data_buf;
    zx_paddr_t info_buf_paddr, data_buf_paddr;
} aml_raw_nand_t;

static inline void set_bits(uint32_t* _reg, const uint32_t _value,
                            const uint32_t _start, const uint32_t _len) {
    writel(((readl(_reg) & ~(((1L << (_len)) - 1) << (_start))) | ((uint32_t)((_value) & ((1L << (_len)) - 1)) << (_start))), _reg);
}

static inline void nandctrl_set_cfg(aml_raw_nand_t* raw_nand,
                                    uint32_t val) {
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    writel(val, reg + P_NAND_CFG);
}

static inline void nandctrl_set_timing_async(aml_raw_nand_t* raw_nand,
                                             int bus_tim,
                                             int bus_cyc) {
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    set_bits((uint32_t*)(reg + P_NAND_CFG),
             ((bus_cyc & 31) | ((bus_tim & 31) << 5) | (0 << 10)),
             0, 12);
}

static inline void nandctrl_send_cmd(aml_raw_nand_t* raw_nand,
                                     uint32_t cmd) {
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    writel(cmd, reg + P_NAND_CMD);
}

/*
 * Controller ECC, OOB, RAND parameters
 */
struct aml_controller_params {
    int ecc_strength; /* # of ECC bits per ECC page */
    int user_mode;    /* OOB bytes every ECC page or per block ? */
    int rand_mode;    /* Randomize ? */
    int bch_mode;
};

/*
 * In the case where user_mode == 2 (2 OOB bytes per ECC page),
 * the controller adds one of these structs *per* ECC page in
 * the info_buf.
 */
struct __attribute__((packed)) aml_info_format {
    uint16_t info_bytes;
    uint8_t zero_cnt; /* bit0~5 is valid */
    struct ecc_sta {
        uint8_t eccerr_cnt : 6;
        uint8_t notused : 1;
        uint8_t completed : 1;
    } ecc;
    uint32_t reserved;
};

static_assert(sizeof(struct aml_info_format) == 8,
              "sizeof(struct aml_info_format) must be exactly 8 bytes");

typedef struct nand_setup {
    union {
        uint32_t d32;
        struct {
            unsigned cmd : 22;
            unsigned large_page : 1;
            unsigned no_rb : 1;
            unsigned a2 : 1;
            unsigned reserved25 : 1;
            unsigned page_list : 1;
            unsigned sync_mode : 2;
            unsigned size : 2;
            unsigned active : 1;
        } b;
    } cfg;
    uint16_t id;
    uint16_t max;
} nand_setup_t;

typedef struct _nand_cmd {
    uint8_t type;
    uint8_t val;
} nand_cmd_t;

typedef struct _ext_info {
    uint32_t read_info;
    uint32_t new_type;
    uint32_t page_per_blk;
    uint32_t xlc;
    uint32_t ce_mask;
    uint32_t boot_num;
    uint32_t each_boot_pages;
    uint32_t bbt_occupy_pages;
    uint32_t bbt_start_block;
} ext_info_t;

typedef struct _nand_page0 {
    nand_setup_t nand_setup;
    unsigned char page_list[16];
    nand_cmd_t retry_usr[32];
    ext_info_t ext_info;
} nand_page0_t;

#define AML_PAGE0_LEN 384
/*
 * Backup copies of page0 are located every 128 pages,
 * with the last one at 896.
 */
#define AML_PAGE0_STEP 128
#define AML_PAGE0_MAX_ADDR 896
/*
 * NAND timing defaults
 */
#define TREA_MAX_DEFAULT 20
#define RHOH_MIN_DEFAULT 15
