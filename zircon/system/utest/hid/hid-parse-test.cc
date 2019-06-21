// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include <assert.h>
#include <stdio.h>

#include <unistd.h>
#include <unittest/unittest.h>

#include "../../../system/dev/input/hid/hid-parser.h"

// See //system/utest/hid-parser/hid-report-data.cpp for the definitions of the test data.
extern "C" const uint8_t boot_mouse_r_desc[50];
extern "C" const uint8_t trinket_r_desc[173];
extern "C" const uint8_t ps3_ds_r_desc[148];
extern "C" const uint8_t acer12_touch_r_desc[660];
extern "C" const uint8_t eve_tablet_r_desc[28];
extern "C" const uint8_t asus_touch_desc[945];

// All sizes below are in bits. They also include the 8 bits for the report
// ID if the report ID is not 0.
hid_report_size_t boot_mouse_golden_sizes[] = {
    {
        .id = 0,
        .in_size   = 24,
        .out_size  = 0,
        .feat_size = 0,
    },
};

hid_reports_t boot_mouse_golden = {
    .sizes = boot_mouse_golden_sizes,
    .sizes_len = 1,
    .num_reports = 1,
    .has_rpt_id = 0,
};

hid_report_size_t trinket_golden_sizes[] = {
    {
        .id = 1,
        .in_size   = 32,
        .out_size  = 0,
        .feat_size = 0,
    },
    {
        .id = 2,
        .in_size   = 64,
        .out_size  = 16,
        .feat_size = 0,
    },
    {
        .id = 3,
        .in_size   = 24,
        .out_size  = 0,
        .feat_size = 0,
    },
    {
        .id = 4,
        .in_size   = 16,
        .out_size  = 0,
        .feat_size = 0,
    },
};

hid_reports_t trinket_golden = {
    .sizes = trinket_golden_sizes,
    .sizes_len = 4,
    .num_reports = 4,
    .has_rpt_id = 1,
};

hid_report_size_t eve_tablet_golden_sizes[] = {
    {
        .id = 0,
        .in_size   = 8,
        .out_size  = 0,
        .feat_size = 0,
    },
};

hid_reports_t eve_tablet_golden = {
    .sizes = eve_tablet_golden_sizes,
    .sizes_len = 1,
    .num_reports = 1,
    .has_rpt_id = 0,
};

hid_report_size_t ps3_golden_sizes[] = {
    {
        .id = 1,
        .in_size   = 392,
        .out_size  = 392,
        .feat_size = 392,
    },
    {
        .id = 2,
        .in_size   = 0,
        .out_size  = 0,
        .feat_size = 392,
    },
    {
        .id = 238,
        .in_size   = 0,
        .out_size  = 0,
        .feat_size = 392,
    },
    {
        .id = 239,
        .in_size   = 0,
        .out_size  = 0,
        .feat_size = 392,
    },
};

hid_reports_t ps3_golden = {
    .sizes = ps3_golden_sizes,
    .sizes_len = 4,
    .num_reports = 4,
    .has_rpt_id = 1,
};

hid_report_size_t acer12_golden_sizes[] = {
    {
        .id = 1,
        .in_size   = 488,
        .out_size  = 0,
        .feat_size = 0,
    },
    {
        .id = 10,
        .in_size   = 0,
        .out_size  = 0,
        .feat_size = 16,
    },
    {
        .id = 14,
        .in_size   = 0,
        .out_size  = 0,
        .feat_size = 2056,
    },
    {
        .id = 2,
        .in_size   = 520,
        .out_size  = 0,
        .feat_size = 0,
    },
    {
        .id = 3,
        .in_size   = 0,
        .out_size  = 264,
        .feat_size = 0,
    },
    {
        .id = 6,
        .in_size   = 0,
        .out_size  = 152,
        .feat_size = 32,
    },
    {
        .id = 4,
        .in_size   = 160,
        .out_size  = 0,
        .feat_size = 0,
    },
    {
        .id = 7,
        .in_size   = 64,
        .out_size  = 0,
        .feat_size = 0,
    },
    {
        .id = 23,
        .in_size   = 256,
        .out_size  = 0,
        .feat_size = 0,
    },

};

hid_reports_t acer12_golden = {
    .sizes = acer12_golden_sizes,
    .sizes_len = 9,
    .num_reports = 9,
    .has_rpt_id = 1,
};

hid_report_size_t asus_golden_sizes[] = {
    {
        .id = 1,
        .in_size   = 928,
        .out_size  = 0,
        .feat_size = 0,
    },
    {
        .id = 10,
        .in_size   = 0,
        .out_size  = 0,
        .feat_size = 16,
    },
    {
        .id = 68,
        .in_size   = 0,
        .out_size  = 0,
        .feat_size = 2056,
    },
    {
        .id = 2,
        .in_size   = 520,
        .out_size  = 0,
        .feat_size = 0,
    },
    {
        .id = 3,
        .in_size   = 0,
        .out_size  = 512,
        .feat_size = 0,
    },
    {
        .id = 4,
        .in_size   = 160,
        .out_size  = 0,
        .feat_size = 0,
    },
};
hid_reports_t asus_golden = {
    .sizes = asus_golden_sizes,
    .sizes_len = 6,
    .num_reports = 6,
    .has_rpt_id = 1,
};

static void print_hid_device_reports_compare(hid_reports_t* reps_a, hid_reports_t* reps_b) {
    printf("\n");
    for (int i = 0; i < (int)reps_a->num_reports; i++) {
        hid_report_size_t rep_a = reps_a->sizes[i];
        hid_report_size_t rep_b = reps_b->sizes[i];
        printf("Report # %d\n", i);
        printf("Report id %d %d\n", rep_a.id, rep_b.id);
        printf("Report IN %d %d\n", rep_a.in_size, rep_b.in_size);
        printf("Report OUT %d %d\n", rep_a.out_size, rep_b.out_size);
        printf("Report FEAT %d %d\n", rep_a.feat_size, rep_b.feat_size);
    }
}

#define NUM_REPORTS 100

hid_reports_t* allocate_hid_reports() {
    hid_reports_t* reports = (hid_reports_t*)calloc(1, sizeof(hid_reports_t));
    if (reports == nullptr) {
        return nullptr;
    }
    reports->sizes = (hid_report_size_t*)calloc(NUM_REPORTS, sizeof(hid_report_size_t));
    if (reports->sizes == nullptr) {
        return nullptr;
    }
    reports->sizes_len = NUM_REPORTS;
    return reports;
}

static bool parse_device(const uint8_t* dev_desc, int size, hid_reports_t *golden_reports) {
    BEGIN_TEST;

    hid_reports_t* hid_reports = allocate_hid_reports();
    ASSERT_NONNULL(hid_reports);

    auto status = hid_lib_parse_reports(dev_desc, size, hid_reports);
    ASSERT_EQ(status, ZX_OK);

    ASSERT_EQ(hid_reports->num_reports, golden_reports->num_reports);
    for (int i = 0; i < (int)golden_reports->num_reports; i++) {
        ASSERT_EQ(hid_reports->sizes[i].id,
                  golden_reports->sizes[i].id);
        ASSERT_EQ(hid_reports->sizes[i].in_size,
                  golden_reports->sizes[i].in_size);
        ASSERT_EQ(hid_reports->sizes[i].out_size,
                  golden_reports->sizes[i].out_size);
        ASSERT_EQ(hid_reports->sizes[i].feat_size,
                  golden_reports->sizes[i].feat_size);
    }
    END_TEST;
}

static bool parse_trinket() {
    return parse_device(trinket_r_desc, sizeof(trinket_r_desc), &trinket_golden);
}

static bool parse_boot_mouse() {
    return parse_device(boot_mouse_r_desc, sizeof(boot_mouse_r_desc), &boot_mouse_golden);
}

static bool parse_eve_tablet() {
    return parse_device(eve_tablet_r_desc, sizeof(eve_tablet_r_desc), &eve_tablet_golden);
}

static bool parse_ps3() {
    return parse_device(ps3_ds_r_desc, sizeof(ps3_ds_r_desc), &ps3_golden);
}
static bool parse_acer12() {
    return parse_device(acer12_touch_r_desc, sizeof(acer12_touch_r_desc), &acer12_golden);
}
static bool parse_asus() {
    return parse_device(asus_touch_desc, sizeof(asus_touch_desc), &asus_golden);
}

BEGIN_TEST_CASE(hid_tests)
RUN_TEST(parse_boot_mouse)
RUN_TEST(parse_trinket)
RUN_TEST(parse_eve_tablet)
RUN_TEST(parse_ps3)
RUN_TEST(parse_acer12)
RUN_TEST(parse_asus)
END_TEST_CASE(hid_tests)
