// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>

#include "test.h"
#include "test-resources.h"

namespace board_test {
namespace {

constexpr pbus_gpio_t child_1_gpios[] = {
    {
        .gpio = TEST_GPIO_1,
    },
};

constexpr pbus_gpio_t child_2_gpios[] = {
    {
        .gpio = TEST_GPIO_2,
    },
    {
        .gpio = TEST_GPIO_3,
    },
};

constexpr pbus_gpio_t child_3_gpios[] = {
    {
        .gpio = TEST_GPIO_4,
    },
};

} // namespace

zx_status_t TestBoard::TestInit() {
    pbus_dev_t child_1_kids[2] = {};
    // Resources for child-2
    child_1_kids[0].gpio_list = child_2_gpios;
    child_1_kids[0].gpio_count = countof(child_2_gpios);

    // Resources for child-3
    child_1_kids[1].gpio_list = child_3_gpios;
    child_1_kids[1].gpio_count = countof(child_3_gpios);

    pbus_dev_t parent_kids[1] = {};
    // Resources for child-1
    parent_kids[0].gpio_list = child_1_gpios;
    parent_kids[0].gpio_count = countof(child_1_gpios);
    parent_kids[0].child_list = child_1_kids;
    parent_kids[0].child_count = countof(child_1_kids);

    pbus_dev_t test_dev = {};
    test_dev.name = "test-parent";
    test_dev.vid = PDEV_VID_TEST;
    test_dev.pid = PDEV_PID_PBUS_TEST;
    test_dev.did = PDEV_DID_TEST_PARENT;
    test_dev.child_list = parent_kids;
    test_dev.child_count = countof(parent_kids);

    return pbus_.DeviceAdd(&test_dev);
}

} // namespace board_test
