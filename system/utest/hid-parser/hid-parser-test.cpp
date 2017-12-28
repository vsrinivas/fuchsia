// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>

#include <hid-parser/item.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>

#include <unistd.h>
#include <unittest/unittest.h>

// See hid-report-data.cpp for the definitions of the test data.
extern "C" const uint8_t boot_mouse_r_desc[50];
extern "C" const uint8_t trinket_r_desc[173];
extern "C" const uint8_t ps3_ds_r_desc[148];
extern "C" const uint8_t acer12_touch_r_desc[660];

namespace {
struct Stats {
    int input_count;
    int collection[2];
};

size_t ItemizeHIDReportDesc(const uint8_t* rpt_desc, size_t desc_len, Stats* stats) {
    const uint8_t* buf = rpt_desc;
    size_t len = desc_len;
    while (len > 0) {
        size_t actual = 0;
        auto item = hid::Item::ReadNext(buf, len, &actual);
        if ((actual > len) || (actual == 0))
            break;

        if (item.tag() == hid::Item::Tag::kEndCollection)
            stats->collection[1]++;
        else if (item.tag() == hid::Item::Tag::kCollection)
            stats->collection[0]++;

        if (item.type() == hid::Item::Type::kMain && item.tag() == hid::Item::Tag::kInput)
            stats->input_count++;

        len -= actual;
        buf += actual;
    }

    return (desc_len - len);
}

}  // namespace.

static bool itemize_acer12_rpt1() {
    BEGIN_TEST;

    Stats stats = {};
    auto len = sizeof(acer12_touch_r_desc);
    auto consumed = ItemizeHIDReportDesc(acer12_touch_r_desc, len, &stats);

    ASSERT_EQ(consumed, len);
    ASSERT_EQ(stats.input_count, 45);
    ASSERT_EQ(stats.collection[0], 13);
    ASSERT_EQ(stats.collection[1], 13);

    END_TEST;
}

static bool parse_boot_mouse() {
    BEGIN_TEST;

    hid::DeviceDescriptor* dev = nullptr;
    auto res = hid::ParseReportDescriptor(
        boot_mouse_r_desc, sizeof(boot_mouse_r_desc), &dev);

    ASSERT_EQ(res, hid::ParseResult::kParseOk);

    // A single report with id zero, this means no report id.
    ASSERT_EQ(dev->rep_count, 1u);
    EXPECT_EQ(dev->report[0].report_id, 0);

    // The only report has 6 fields.
    EXPECT_EQ(dev->report[0].count, 6);
    const auto fields = dev->report[0].first_field;

    // All fields are input type with report id = 0.
    for (uint8_t ix = 0; ix != dev->report[0].count; ++ix) {
        EXPECT_EQ(fields[ix].report_id, 0);
        EXPECT_EQ(fields[ix].type, hid::kInput);
    }

    // First 3 fields are the buttons, with usages 1, 2, 3, in the button page.
    auto expected_flags = hid::kData | hid::kAbsolute | hid::kScalar;

    for (uint8_t ix = 0; ix != 3; ++ix) {
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kButton);
        EXPECT_EQ(fields[ix].attr.usage.usage, ix + 1);
        EXPECT_EQ(fields[ix].attr.bit_sz, 1);
        EXPECT_EQ(fields[ix].attr.logc_mm.min, 0);
        EXPECT_EQ(fields[ix].attr.logc_mm.max, 1);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    // Next field is 5 bits constant. Aka padding.
    EXPECT_EQ(fields[3].attr.bit_sz, 5);
    EXPECT_EQ(hid::kConstant & fields[3].flags, hid::kConstant);

    // Next comes 'X' field, 8 bits data, relative.
    expected_flags = hid::kData | hid::kRelative | hid::kScalar;

    EXPECT_EQ(fields[4].attr.usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(fields[4].attr.usage.usage, hid::usage::GenericDesktop::kX);
    EXPECT_EQ(fields[4].attr.bit_sz, 8);
    EXPECT_EQ(fields[4].attr.logc_mm.min, -127);
    EXPECT_EQ(fields[4].attr.logc_mm.max, 127);
    EXPECT_EQ(fields[4].attr.phys_mm.min, 0);
    EXPECT_EQ(fields[4].attr.phys_mm.max, 0);
    EXPECT_EQ(expected_flags & fields[4].flags, expected_flags);

    // Last comes 'Y' field, same as 'X'.
    EXPECT_EQ(fields[5].attr.usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(fields[5].attr.usage.usage, hid::usage::GenericDesktop::kY);
    EXPECT_EQ(fields[5].attr.bit_sz, 8);
    EXPECT_EQ(fields[5].attr.logc_mm.min, -127);
    EXPECT_EQ(fields[5].attr.logc_mm.max, 127);
    EXPECT_EQ(fields[5].attr.phys_mm.min, 0);
    EXPECT_EQ(fields[5].attr.phys_mm.max, 0);
    EXPECT_EQ(expected_flags & fields[4].flags, expected_flags);

    // Now test the collections.
    // Inner collection is physical GeneticDesktop|Pointer.
    auto collection = fields[0].col;
    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kPhysical);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, hid::usage::GenericDesktop::kPointer);

    // Outer collection is the application.
    collection = collection->parent;
    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kApplication);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, hid::usage::GenericDesktop::kMouse);

    // No parent collection.
    EXPECT_TRUE(collection->parent == nullptr);
    END_TEST;
}

static bool parse_adaf_trinket() {
    BEGIN_TEST;

    hid::DeviceDescriptor* dev = nullptr;
    auto res = hid::ParseReportDescriptor(
        trinket_r_desc, sizeof(trinket_r_desc), &dev);

    ASSERT_EQ(res, hid::ParseResult::kParseOk);

    // Four different reports
    ASSERT_EQ(dev->rep_count, 4u);

    //////////////////////////////////////////////////////////////////////////////////
    // First report is the same as boot_mouse, except for the report id.
    EXPECT_EQ(dev->report[0].report_id, 1);
    ASSERT_EQ(dev->report[0].count, 6);
    const hid::ReportField* fields = dev->report[0].first_field;

    // All fields are scalar input type with report id = 0.
    for (uint8_t ix = 0; ix != dev->report[0].count; ++ix) {
        EXPECT_EQ(fields[ix].report_id, 1);
        EXPECT_EQ(fields[ix].type, hid::kInput);
        EXPECT_EQ(hid::kScalar & fields[ix].flags, hid::kScalar);
    }

    // First 3 fields are the buttons, with usages 1, 2, 3, in the button page.
    auto expected_flags = hid::kData | hid::kAbsolute;

    for (uint8_t ix = 0; ix != 3; ++ix) {
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kButton);
        EXPECT_EQ(fields[ix].attr.usage.usage, ix + 1);
        EXPECT_EQ(fields[ix].attr.bit_sz, 1);
        EXPECT_EQ(fields[ix].attr.logc_mm.min, 0);
        EXPECT_EQ(fields[ix].attr.logc_mm.max, 1);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    // Next field is 5 bits constant. Aka padding.
    EXPECT_EQ(fields[3].attr.bit_sz, 5);
    EXPECT_EQ(hid::kConstant & fields[3].flags, hid::kConstant);

    // Next comes 'X' field, 8 bits data, relative.
    expected_flags = hid::kData | hid::kRelative;

    EXPECT_EQ(fields[4].attr.usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(fields[4].attr.usage.usage, hid::usage::GenericDesktop::kX);
    EXPECT_EQ(fields[4].attr.bit_sz, 8);
    EXPECT_EQ(fields[4].attr.logc_mm.min, -127);
    EXPECT_EQ(fields[4].attr.logc_mm.max, 127);
    EXPECT_EQ(expected_flags & fields[4].flags, expected_flags);

    // Last comes 'Y' field, same as 'X'.
    EXPECT_EQ(fields[5].attr.usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(fields[5].attr.usage.usage, hid::usage::GenericDesktop::kY);
    EXPECT_EQ(fields[5].attr.bit_sz, 8);
    EXPECT_EQ(fields[5].attr.logc_mm.min, -127);
    EXPECT_EQ(fields[5].attr.logc_mm.max, 127);
    EXPECT_EQ(expected_flags & fields[4].flags, expected_flags);

    // Now test the collections.
    // Inner collection is physical GeneticDesktop|Pointer.
    auto collection = fields[0].col;
    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kPhysical);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, hid::usage::GenericDesktop::kPointer);

    // Outer collection is the application.
    collection = collection->parent;
    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kApplication);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, hid::usage::GenericDesktop::kMouse);

    // No parent collection.
    EXPECT_TRUE(collection->parent == nullptr);

    //////////////////////////////////////////////////////////////////////////////////
    // Second  report is a keyboard with 20 fields.
    EXPECT_EQ(dev->report[1].report_id, 2);
    ASSERT_EQ(dev->report[1].count, 20);

    fields = dev->report[1].first_field;

    // First 8 are input bits with usages 0xe0 to 0xe7 on the keyboard page.
    expected_flags = hid::kData | hid::kAbsolute | hid::kScalar;

    for (uint8_t ix = 0; ix != 8; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kInput);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kKeyboardKeypad);
        EXPECT_EQ(fields[ix].attr.usage.usage, ix + 0xe0);
        EXPECT_EQ(fields[ix].attr.bit_sz, 1);
        EXPECT_EQ(fields[ix].attr.logc_mm.min, 0);
        EXPECT_EQ(fields[ix].attr.logc_mm.max, 1);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    // Next field is 8 bits padding (input).
    EXPECT_EQ(fields[8].attr.bit_sz, 8);
    EXPECT_EQ(fields[8].type, hid::kInput);
    EXPECT_EQ(hid::kConstant & fields[8].flags, hid::kConstant);

    // Next 5 fields are the LED bits output, with usages NumLock(1) to Kana(5).
    auto led_usage = static_cast<uint16_t>(hid::usage::LEDs::kNumLock);

    for (uint8_t ix = 9; ix != 14; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kOutput);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kLEDs);
        EXPECT_EQ(fields[ix].attr.bit_sz, 1);
        EXPECT_EQ(fields[ix].attr.usage.usage, led_usage++);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    // Next field is 3 bits padding (output).
    EXPECT_EQ(fields[14].attr.bit_sz, 3);
    EXPECT_EQ(fields[14].type, hid::kOutput);
    EXPECT_EQ(hid::kConstant & fields[14].flags, hid::kConstant);

    // Next 5 fields are byte-sized key input array.
    expected_flags = hid::kData | hid::kAbsolute | hid::kArray;

    for (uint8_t ix = 15; ix != 20; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kInput);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kKeyboardKeypad);
        EXPECT_EQ(fields[ix].attr.bit_sz, 8);
        EXPECT_EQ(fields[ix].attr.usage.usage, 0);
        EXPECT_EQ(fields[ix].attr.logc_mm.min, 0);
        EXPECT_EQ(fields[ix].attr.logc_mm.max, 164);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    // All fields belong to the same collection
    collection = fields[0].col;

    for (uint8_t ix = 1; ix != 20; ++ix) {
        EXPECT_TRUE(fields[ix].col == collection);
    }

    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kApplication);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, hid::usage::GenericDesktop::kKeyboard);
    // No parent collection.
    EXPECT_TRUE(collection->parent == nullptr);

    //////////////////////////////////////////////////////////////////////////////////
    // Third report, single 16 bit input array field (consumer control).
    EXPECT_EQ(dev->report[2].report_id, 3);
    ASSERT_EQ(dev->report[2].count, 1);

    fields = dev->report[2].first_field;

    expected_flags = hid::kData | hid::kAbsolute | hid::kArray;

    EXPECT_EQ(fields[0].type, hid::kInput);
    EXPECT_EQ(fields[0].attr.usage.page, hid::usage::Page::kConsumer);
    EXPECT_EQ(fields[0].attr.usage.usage, 0);
    EXPECT_EQ(fields[0].attr.logc_mm.min, 0);
    EXPECT_EQ(fields[0].attr.logc_mm.max, 572);
    EXPECT_EQ(fields[0].attr.bit_sz, 16);
    EXPECT_EQ(expected_flags & fields[0].flags, expected_flags);

    collection = fields[0].col;
    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kApplication);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kConsumer);
    EXPECT_EQ(collection->usage.usage, hid::usage::Consumer::kConsumerControl);
    // No parent collection.
    EXPECT_TRUE(collection->parent == nullptr);

    //////////////////////////////////////////////////////////////////////////////////
    // Fourth report is a 2 bit input (system control: sleep, wake-up, power-down)

    EXPECT_EQ(dev->report[3].report_id, 4);
    ASSERT_EQ(dev->report[3].count, 2);

    fields = dev->report[3].first_field;

    // First field is a 2 bit input array.
    expected_flags = hid::kData | hid::kAbsolute | hid::kArray;

    EXPECT_EQ(fields[0].type, hid::kInput);
    EXPECT_EQ(fields[0].attr.usage.page, hid::usage::Page::kGenericDesktop);
    // TODO(cpu): The |usage.usage| as parsed is incorrect. In this particular
    // case as the array input 1,2,3 should map to 0x82, 0x81, 0x83 which is not currently
    // supported in the model.
    EXPECT_EQ(fields[0].attr.usage.usage, hid::usage::GenericDesktop::kSystemSleep);
    EXPECT_EQ(fields[0].attr.logc_mm.min, 1);
    EXPECT_EQ(fields[0].attr.logc_mm.max, 3);
    EXPECT_EQ(fields[0].attr.bit_sz, 2);
    EXPECT_EQ(expected_flags & fields[0].flags, expected_flags);

    // Last field is 6 bits padding (output).
    EXPECT_EQ(fields[1].attr.bit_sz, 6);
    EXPECT_EQ(fields[1].type, hid::kInput);
    EXPECT_EQ(hid::kConstant & fields[1].flags, hid::kConstant);

    collection = fields[0].col;
    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kApplication);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, hid::usage::GenericDesktop::kSystemControl);
    // No parent collection.
    EXPECT_TRUE(collection->parent == nullptr);

    END_TEST;
}

static bool parse_ps3_controller() {
    BEGIN_TEST;

    hid::DeviceDescriptor* dev = nullptr;
    auto res = hid::ParseReportDescriptor(
        ps3_ds_r_desc, sizeof(ps3_ds_r_desc), &dev);

    ASSERT_EQ(res, hid::ParseResult::kParseOk);
    // Four different reports
    ASSERT_EQ(dev->rep_count, 4u);

    //////////////////////////////////////////////////////////////////////////////////
    // First report has 172 fields!!
    EXPECT_EQ(dev->report[0].report_id, 1);
    ASSERT_EQ(dev->report[0].count, 172);
    const hid::ReportField* fields = dev->report[0].first_field;

    // First field is 8 bits, constant GenericDesktop page, but no usage described.
    // being it is a version number?
    auto expected_flags = hid::kConstant | hid::kAbsolute | hid::kScalar;

    EXPECT_EQ(fields[0].type, hid::kInput);
    EXPECT_EQ(fields[0].attr.usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(fields[0].attr.usage.usage, 0);
    EXPECT_EQ(fields[0].attr.logc_mm.min, 0);
    EXPECT_EQ(fields[0].attr.logc_mm.max, 255);
    EXPECT_EQ(fields[0].attr.bit_sz, 8);
    EXPECT_EQ(expected_flags & fields[0].flags, expected_flags);

    // Next 19 fields are one-bit input representing the buttons.
    expected_flags = hid::kData | hid::kAbsolute | hid::kScalar;

    for (uint8_t ix = 1; ix != 20; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kInput);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kButton);
        EXPECT_EQ(fields[ix].attr.usage.usage, ix);
        EXPECT_EQ(fields[ix].attr.bit_sz, 1);
        EXPECT_EQ(fields[ix].attr.logc_mm.min, 0);
        EXPECT_EQ(fields[ix].attr.logc_mm.max, 1);
        EXPECT_EQ(fields[ix].attr.phys_mm.min, 0);
        EXPECT_EQ(fields[ix].attr.phys_mm.max, 1);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    // The next 13 fields are 13 bits of constant, vendor-defined. Probably padding.
    for (uint8_t ix = 20; ix != 33; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kInput);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kVendorDefinedStart);
        EXPECT_EQ(fields[ix].attr.usage.usage, 0);
        EXPECT_EQ(fields[ix].attr.bit_sz, 1);
        EXPECT_EQ(hid::kConstant & fields[ix].flags, hid::kConstant);
    }

    expected_flags = hid::kData | hid::kAbsolute | hid::kScalar;

    // Next four 8-bit input fields are X,Y, Z and Rz.
    for (uint8_t ix = 33; ix != 37; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kInput);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kGenericDesktop);
        EXPECT_EQ(fields[ix].attr.bit_sz, 8);
        EXPECT_EQ(fields[ix].attr.logc_mm.min, 0);
        EXPECT_EQ(fields[ix].attr.logc_mm.max, 255);
        EXPECT_EQ(fields[ix].attr.phys_mm.min, 0);
        EXPECT_EQ(fields[ix].attr.phys_mm.max, 255);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    EXPECT_EQ(fields[33].attr.usage.usage, hid::usage::GenericDesktop::kX);
    EXPECT_EQ(fields[34].attr.usage.usage, hid::usage::GenericDesktop::kY);
    EXPECT_EQ(fields[35].attr.usage.usage, hid::usage::GenericDesktop::kZ);
    EXPECT_EQ(fields[36].attr.usage.usage, hid::usage::GenericDesktop::kRz);

    // Next 39 fields are input, 8-bit pointer scalar data.
    for (uint8_t ix = 37; ix != 76; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kInput);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kGenericDesktop);
        EXPECT_EQ(fields[ix].attr.usage.usage, hid::usage::GenericDesktop::kPointer);
        EXPECT_EQ(fields[ix].attr.bit_sz, 8);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    // Next 48 are 8-bit scalar output pointer data.
    for (uint8_t ix = 76; ix != 124; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kOutput);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kGenericDesktop);
        EXPECT_EQ(fields[ix].attr.usage.usage, hid::usage::GenericDesktop::kPointer);
        EXPECT_EQ(fields[ix].attr.bit_sz, 8);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    // Last 48 are 8-bit scalar feature pointer data.
    for (uint8_t ix = 124; ix != 172; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kFeature);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kGenericDesktop);
        EXPECT_EQ(fields[ix].attr.usage.usage, hid::usage::GenericDesktop::kPointer);
        EXPECT_EQ(fields[ix].attr.bit_sz, 8);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    //////////////////////////////////////////////////////////////////////////////////
    // Second report has 48 fields. It is pretty much identical to last 48 fields
    // of the first report.

    EXPECT_EQ(dev->report[1].report_id, 2);
    ASSERT_EQ(dev->report[1].count, 48);
    fields = dev->report[1].first_field;

    expected_flags = hid::kData | hid::kAbsolute | hid::kScalar;

    for (uint8_t ix = 0; ix != 48; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kFeature);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kGenericDesktop);
        EXPECT_EQ(fields[ix].attr.usage.usage, hid::usage::GenericDesktop::kPointer);
        EXPECT_EQ(fields[ix].attr.bit_sz, 8);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    //////////////////////////////////////////////////////////////////////////////////
    // Third report is same as the second one except for report id.

    EXPECT_EQ(dev->report[2].report_id, 0xee);
    ASSERT_EQ(dev->report[2].count, 48);
    fields = dev->report[2].first_field;

    for (uint8_t ix = 0; ix != 48; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kFeature);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kGenericDesktop);
        EXPECT_EQ(fields[ix].attr.usage.usage, hid::usage::GenericDesktop::kPointer);
        EXPECT_EQ(fields[ix].attr.bit_sz, 8);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    //////////////////////////////////////////////////////////////////////////////////
    // Fourth report is same as the second one except for report id.

    EXPECT_EQ(dev->report[3].report_id, 0xef);
    ASSERT_EQ(dev->report[3].count, 48);
    fields = dev->report[3].first_field;

    for (uint8_t ix = 0; ix != 48; ++ix) {
        EXPECT_EQ(fields[ix].type, hid::kFeature);
        EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kGenericDesktop);
        EXPECT_EQ(fields[ix].attr.usage.usage, hid::usage::GenericDesktop::kPointer);
        EXPECT_EQ(fields[ix].attr.bit_sz, 8);
        EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
    }

    // Collections test
    //
    // In the first report, The X,Y,Z, Rz fields are in a 3-level
    // deep collection physical -> logical -> app. Test that.
    auto collection = dev->report[0].first_field[33].col;

    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kPhysical);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, hid::usage::GenericDesktop::kPointer);

    collection = collection->parent;
    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kLogical);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, 0);

    collection = collection->parent;
    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kApplication);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, hid::usage::GenericDesktop::kJoystick);
    EXPECT_TRUE(collection->parent == nullptr);

    // The second report first field is in a logical -> app collection.
    collection = dev->report[1].first_field[0].col;

    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kLogical);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, 0);

    collection = collection->parent;
    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kApplication);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, hid::usage::GenericDesktop::kJoystick);
    EXPECT_TRUE(collection->parent == nullptr);

    // The third report is the same as the second. This seems a trivial test
    // but previous parsers failed this one.
    collection = dev->report[2].first_field[0].col;

    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kLogical);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, 0);

    collection = collection->parent;
    ASSERT_TRUE(collection != nullptr);
    EXPECT_EQ(collection->type, hid::CollectionType::kApplication);
    EXPECT_EQ(collection->usage.page, hid::usage::Page::kGenericDesktop);
    EXPECT_EQ(collection->usage.usage, hid::usage::GenericDesktop::kJoystick);
    EXPECT_TRUE(collection->parent == nullptr);

    END_TEST;
}

static bool parse_acer12_touch() {
    BEGIN_TEST;

    hid::DeviceDescriptor* dd = nullptr;
    auto res = hid::ParseReportDescriptor(
        acer12_touch_r_desc, sizeof(acer12_touch_r_desc), &dd);

    EXPECT_EQ(res, hid::ParseResult::kParseOk);
    END_TEST;
}

BEGIN_TEST_CASE(hidparser_tests)
RUN_TEST(itemize_acer12_rpt1)
RUN_TEST(parse_boot_mouse)
RUN_TEST(parse_adaf_trinket)
RUN_TEST(parse_ps3_controller)
RUN_TEST(parse_acer12_touch)
END_TEST_CASE(hidparser_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
