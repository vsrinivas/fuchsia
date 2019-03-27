// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <usb/usb.h>

#include <unittest/unittest.h>

namespace {

// Raw descriptors obtained from a Pixelbook with a USB flash drive connected to it.
// To re-generate this, merge the tool at commit
// b934c6e31e31a291b3761383b28cd1d0004e5423 on sandbox/idwmaster/ums-descriptor-debugger
// Be careful to not accidentally submit this tool in a CL
// (it is meant for debugging, not production use).
// to run the tool; simply type the command: debug
// in your device's terminal after connecting a USB mass storage device.
// The raw descriptor dump will be put in /data/debug
// You can copy this to your PC with fx cp
// and convert to an unsigned char array with your favorite conversion script.
constexpr unsigned char kDescriptors[] = {
    // Data from a real USB flash drive
    9, 4, 0, 0, 2, 8, 6, 80, 0, 7, 5, 129, 2,
    0, 4, 0, 6, 48, 3, 0, 0, 0, 7, 5, 2, 2, 0,
    4, 0, 6, 48, 3, 0, 0, 0,
    // Synthetic data to test alternate interfaces
    9, 4, 0, 1, 2, 8, 6, 80, 0};

constexpr usb_interface_descriptor_t kParsedDescriptors[] = {
    // Data from a real USB flash drive
    {9, 4, 0, 0, 2, 8, 6, 80, 0},
    // Synthetic data to test alternate interfaces
    {9, 4, 0, 1, 2, 8, 6, 80, 0}};

const usb_endpoint_descriptor_t kEndpointDescriptors[] = {
    {7, 5, 129, 2, 1024, 0},
    {7, 5, 2, 2, 1024, 0}};

static void GetDescriptors(
    void* ctx,
    void* out_descs_buffer,
    size_t descs_size,
    size_t* out_descs_actual) {
    if (descs_size != sizeof(kDescriptors)) {
        return;
    }
    memcpy(out_descs_buffer, kDescriptors, descs_size);
    *out_descs_actual = descs_size;
}

static size_t GetDescriptorsLength(void* context) {
    return sizeof(kDescriptors);
}

bool InterfaceListTest() {
    BEGIN_TEST;
    usb_protocol_ops_t ops;
    ops.get_descriptors_length = GetDescriptorsLength;
    ops.get_descriptors = GetDescriptors;
    usb_protocol_t proto = {};
    proto.ops = &ops;
    usb::InterfaceList list(&proto, true);
    ASSERT_EQ(ZX_OK, list.check(), "");
    size_t count = 0;
    for (auto interface : list) {
        ASSERT_EQ(
            0,
            memcmp(kParsedDescriptors + count, interface.descriptor(),
                   sizeof(usb_interface_descriptor_t)),
            "");
        size_t endpoint_count = 0;
        for (auto endpoint : interface) {
            ASSERT_EQ(
                0,
                memcmp(kEndpointDescriptors + endpoint_count, &endpoint,
                       sizeof(usb_endpoint_descriptor_t)),
                "");
            endpoint_count++;
        }
        ASSERT_EQ(2, endpoint_count, "");
        count++;
    }
    ASSERT_EQ(count, 1, "");
    count = 0;
    usb::InterfaceList list2(&proto);
    for (auto interface : list2) {
        ASSERT_EQ(0,
                  memcmp(kParsedDescriptors + count, interface.descriptor(),
                         sizeof(usb_interface_descriptor_t)),
                  "");
        size_t endpoint_count = 0;
        for (auto endpoint : interface) {
            ASSERT_EQ(0,
                      memcmp(kEndpointDescriptors + endpoint_count,
                             &endpoint,
                             sizeof(usb_endpoint_descriptor_t)),
                      "");
            endpoint_count++;
        }
        ASSERT_EQ(count ? 0U : 2U, endpoint_count, "");
        count++;
    }
    ASSERT_EQ(count, 2, "");
    count = 0;
    for (auto iter = list2.cbegin(); iter != list2.cend(); iter++) {
        auto interface = *iter;
        ASSERT_EQ(0, memcmp(kParsedDescriptors + count, interface.descriptor(), sizeof(usb_interface_descriptor_t)),
                  "");
        size_t endpoint_count = 0;
        for (auto endpoint : interface) {
            ASSERT_EQ(0,
                      memcmp(kEndpointDescriptors + endpoint_count, &endpoint,
                             sizeof(usb_endpoint_descriptor_t)),
                      "");
            endpoint_count++;
        }
        ASSERT_EQ(count ? 0U : 2U, endpoint_count, "");
        count++;
    }
    END_TEST;
}
} // namespace

BEGIN_TEST_CASE(usb_wrapper_tests)
RUN_NAMED_TEST("InterfaceList test", InterfaceListTest)
END_TEST_CASE(usb_wrapper_tests)