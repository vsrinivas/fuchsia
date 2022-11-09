// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid-parser/descriptor.h>
#include <hid/boot.h>
#include <hid/paradise.h>
#include <zxtest/zxtest.h>

TEST(HidDescriptorTest, GetReportsSizeWithIds) {
  size_t len;
  const uint8_t* report_desc = get_paradise_touch_report_desc(&len);

  hid::DeviceDescriptor* desc = nullptr;
  hid::ParseResult res = hid::ParseReportDescriptor(report_desc, len, &desc);
  ASSERT_EQ(res, hid::ParseResult::kParseOk);

  size_t size =
      GetReportSizeFromFirstByte(*desc, hid::ReportType::kReportInput, PARADISE_RPT_ID_STYLUS);
  ASSERT_EQ(size, sizeof(paradise_stylus_t));

  size = GetReportSizeFromFirstByte(*desc, hid::ReportType::kReportInput, PARADISE_RPT_ID_TOUCH);
  ASSERT_EQ(size, sizeof(paradise_touch_t));

  FreeDeviceDescriptor(desc);
}

TEST(HidDescriptorTest, GetReportsSizeNoId) {
  size_t len;
  const uint8_t* report_desc = get_boot_mouse_report_desc(&len);

  hid::DeviceDescriptor* desc = nullptr;
  hid::ParseResult res = hid::ParseReportDescriptor(report_desc, len, &desc);
  ASSERT_EQ(res, hid::ParseResult::kParseOk);

  // First byte doesn't matter since there's only one report.
  size_t size = GetReportSizeFromFirstByte(*desc, hid::ReportType::kReportInput, 0xAB);
  ASSERT_EQ(size, sizeof(hid_boot_mouse_report_t));

  FreeDeviceDescriptor(desc);
}

TEST(HidDescriptorTest, MaxCollectionError) {
  const uint8_t report_desc[] = {
      0xa1, 0x01, 0xa0, 0xA0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0,
      0xa0, 0xa0, 0xa0, 0xa0, 0xA0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0,
      0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xA0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0,
      0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xA0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0,
      0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xA0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0,
      0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xA0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0,
      0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xA0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0,
      0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xA0, 0xa0, 0xa0, 0xa0, 0xa0,
      0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xc0, 0xC0};
  hid::DeviceDescriptor* desc = nullptr;
  hid::ParseResult res = hid::ParseReportDescriptor(report_desc, sizeof(report_desc), &desc);
  ASSERT_EQ(res, hid::ParseResult::kParseOverflow);

  FreeDeviceDescriptor(desc);
}

// There was a memory error where ReportId's were passed around as pointers to a std::vector
// element. If we pushed a report Id and then allocated a lot of new report ids, the std::vector
// would re-allocate the memory, and we would corrupt freed memory when we popped and accessed
// the report Id.
TEST(HidDescriptorTest, ReportIdPopError) {
  const uint8_t report_desc[] = {
      0xA1, 0x01,                    // Collection (Application)
      0x85, 0x01,                    //   Report ID (1)
      0xA5, 0xAA,                    //   Push
      0x85, 0x02,                    //     Report ID (2)
      0x85, 0x03,                    //     Report ID (3)
      0x85, 0x04,                    //     Report ID (4)
      0x85, 0x05,                    //     Report ID (5)
      0x85, 0x06,                    //     Report ID (6)
      0x85, 0x07,                    //     Report ID (7)
      0x85, 0x08,                    //     Report ID (8)
      0x85, 0x09,                    //     Report ID (9)
      0x85, 0x0A,                    //     Report ID (10)
      0x85, 0x0B,                    //     Report ID (11)
      0x85, 0x0C,                    //     Report ID (12)
      0x85, 0x0D,                    //     Report ID (13)
      0x85, 0x0E,                    //     Report ID (14)
      0x85, 0x0F,                    //     Report ID (15)
      0x85, 0x10,                    //     Report ID (16)
      0x85, 0x11,                    //     Report ID (17)
      0x85, 0x12,                    //     Report ID (18)
      0x85, 0x13,                    //     Report ID (19)
      0x85, 0x14,                    //     Report ID (20)
      0x85, 0x15,                    //     Report ID (21)
      0x85, 0x16,                    //     Report ID (22)
      0x85, 0x17,                    //     Report ID (23)
      0x85, 0x18,                    //     Report ID (24)
      0x85, 0x19,                    //     Report ID (25)
      0x85, 0x1A,                    //     Report ID (26)
      0x85, 0x1B,                    //     Report ID (27)
      0x85, 0x1C,                    //     Report ID (28)
      0x85, 0x1D,                    //     Report ID (29)
      0x85, 0x1E,                    //     Report ID (30)
      0x85, 0x1F,                    //     Report ID (31)
      0x85, 0x20,                    //     Report ID (32)
      0x85, 0x21,                    //     Report ID (33)
      0x85, 0x22,                    //     Report ID (34)
      0x85, 0x23,                    //     Report ID (35)
      0x85, 0x24,                    //     Report ID (36)
      0x85, 0x25,                    //     Report ID (37)
      0x85, 0x26,                    //     Report ID (38)
      0x85, 0x27,                    //     Report ID (39)
      0x85, 0x28,                    //     Report ID (40)
      0x85, 0x29,                    //     Report ID (41)
      0x85, 0x2A,                    //     Report ID (42)
      0x85, 0x2B,                    //     Report ID (43)
      0x85, 0x2C,                    //     Report ID (44)
      0x85, 0x2D,                    //     Report ID (45)
      0x85, 0x2E,                    //     Report ID (46)
      0x85, 0x2F,                    //     Report ID (47)
      0x85, 0x30,                    //     Report ID (48)
      0x85, 0x31,                    //     Report ID (49)
      0x85, 0x32,                    //     Report ID (50)
      0x85, 0x33,                    //     Report ID (51)
      0x85, 0x34,                    //     Report ID (52)
      0x85, 0x35,                    //     Report ID (53)
      0x85, 0x36,                    //     Report ID (54)
      0x85, 0x37,                    //     Report ID (55)
      0x85, 0x38,                    //     Report ID (56)
      0x85, 0x39,                    //     Report ID (57)
      0x85, 0x3A,                    //     Report ID (58)
      0x85, 0x3B,                    //     Report ID (59)
      0x85, 0x3C,                    //     Report ID (60)
      0x85, 0x3D,                    //     Report ID (61)
      0x85, 0x3E,                    //     Report ID (62)
      0x85, 0x3F,                    //     Report ID (63)
      0x85, 0x40,                    //     Report ID (64)
      0x85, 0x41,                    //     Report ID (65)
      0x85, 0x42,                    //     Report ID (66)
      0x85, 0x43,                    //     Report ID (67)
      0x85, 0x44,                    //     Report ID (68)
      0x85, 0x45,                    //     Report ID (69)
      0x85, 0x46,                    //     Report ID (70)
      0x85, 0x47,                    //     Report ID (71)
      0x85, 0x48,                    //     Report ID (72)
      0x85, 0x49,                    //     Report ID (73)
      0x85, 0x4A,                    //     Report ID (74)
      0x85, 0x4B,                    //     Report ID (75)
      0x85, 0x4C,                    //     Report ID (76)
      0x85, 0x4D,                    //     Report ID (77)
      0x85, 0x4E,                    //     Report ID (78)
      0x85, 0x4F,                    //     Report ID (79)
      0x85, 0x50,                    //     Report ID (80)
      0x85, 0x51,                    //     Report ID (81)
      0x85, 0x52,                    //     Report ID (82)
      0x85, 0x53,                    //     Report ID (83)
      0x85, 0x54,                    //     Report ID (84)
      0x85, 0x55,                    //     Report ID (85)
      0x85, 0x56,                    //     Report ID (86)
      0x85, 0x57,                    //     Report ID (87)
      0x85, 0x58,                    //     Report ID (88)
      0x85, 0x59,                    //     Report ID (89)
      0x85, 0x5A,                    //     Report ID (90)
      0x85, 0x5B,                    //     Report ID (91)
      0x85, 0x5C,                    //     Report ID (92)
      0x85, 0x5D,                    //     Report ID (93)
      0x85, 0x5E,                    //     Report ID (94)
      0x85, 0x5F,                    //     Report ID (95)
      0x85, 0x60,                    //     Report ID (96)
      0x85, 0x61,                    //     Report ID (97)
      0x85, 0x62,                    //     Report ID (98)
      0x85, 0x63,                    //     Report ID (99)
      0x85, 0x64,                    //     Report ID (100)
      0x85, 0x65,                    //     Report ID (101)
      0x85, 0x66,                    //     Report ID (102)
      0x85, 0x67,                    //     Report ID (103)
      0x85, 0x68,                    //     Report ID (104)
      0x85, 0x69,                    //     Report ID (105)
      0x85, 0x6A,                    //     Report ID (106)
      0x85, 0x6B,                    //     Report ID (107)
      0x85, 0x6C,                    //     Report ID (108)
      0x85, 0x6D,                    //     Report ID (109)
      0x85, 0x6E,                    //     Report ID (110)
      0x85, 0x6F,                    //     Report ID (111)
      0x85, 0x70,                    //     Report ID (112)
      0x85, 0x71,                    //     Report ID (113)
      0x85, 0x72,                    //     Report ID (114)
      0x85, 0x73,                    //     Report ID (115)
      0x85, 0x74,                    //     Report ID (116)
      0x85, 0x75,                    //     Report ID (117)
      0x85, 0x76,                    //     Report ID (118)
      0x85, 0x77,                    //     Report ID (119)
      0x85, 0x78,                    //     Report ID (120)
      0x85, 0x79,                    //     Report ID (121)
      0x85, 0x7A,                    //     Report ID (122)
      0x85, 0x7B,                    //     Report ID (123)
      0x85, 0x7C,                    //     Report ID (124)
      0x85, 0x7D,                    //     Report ID (125)
      0x85, 0x7E,                    //     Report ID (126)
      0x85, 0x7F,                    //     Report ID (127)
      0x85, 0x80,                    //     Report ID (-128)
      0x85, 0x81,                    //     Report ID (-127)
      0x85, 0x82,                    //     Report ID (-126)
      0x85, 0x83,                    //     Report ID (-125)
      0x85, 0x84,                    //     Report ID (-124)
      0x85, 0x85,                    //     Report ID (-123)
      0x85, 0x86,                    //     Report ID (-122)
      0x85, 0x87,                    //     Report ID (-121)
      0x85, 0x88,                    //     Report ID (-120)
      0x85, 0x89,                    //     Report ID (-119)
      0x85, 0x8A,                    //     Report ID (-118)
      0x85, 0x8B,                    //     Report ID (-117)
      0x85, 0x8C,                    //     Report ID (-116)
      0x85, 0x8D,                    //     Report ID (-115)
      0x85, 0x8E,                    //     Report ID (-114)
      0x85, 0x8F,                    //     Report ID (-113)
      0x85, 0x90,                    //     Report ID (-112)
      0x85, 0x91,                    //     Report ID (-111)
      0x85, 0x92,                    //     Report ID (-110)
      0x85, 0x93,                    //     Report ID (-109)
      0x85, 0x94,                    //     Report ID (-108)
      0x85, 0x95,                    //     Report ID (-107)
      0x85, 0x96,                    //     Report ID (-106)
      0x85, 0x97,                    //     Report ID (-105)
      0x85, 0x98,                    //     Report ID (-104)
      0x85, 0x99,                    //     Report ID (-103)
      0x85, 0x9A,                    //     Report ID (-102)
      0x85, 0x9B,                    //     Report ID (-101)
      0x85, 0x9C,                    //     Report ID (-100)
      0x85, 0x9D,                    //     Report ID (-99)
      0x85, 0x9E,                    //     Report ID (-98)
      0x85, 0x9F,                    //     Report ID (-97)
      0x85, 0xA0,                    //     Report ID (-96)
      0x85, 0xA1,                    //     Report ID (-95)
      0x85, 0xA2,                    //     Report ID (-94)
      0x85, 0xA3,                    //     Report ID (-93)
      0x85, 0xA4,                    //     Report ID (-92)
      0x85, 0xA5,                    //     Report ID (-91)
      0x85, 0xA6,                    //     Report ID (-90)
      0x85, 0xA7,                    //     Report ID (-89)
      0x85, 0xA8,                    //     Report ID (-88)
      0x85, 0xA9,                    //     Report ID (-87)
      0x85, 0xAA,                    //     Report ID (-86)
      0x85, 0xAB,                    //     Report ID (-85)
      0x85, 0xAC,                    //     Report ID (-84)
      0x85, 0xAD,                    //     Report ID (-83)
      0x85, 0xAE,                    //     Report ID (-82)
      0x85, 0xAF,                    //     Report ID (-81)
      0x85, 0xB0,                    //     Report ID (-80)
      0x85, 0xB1,                    //     Report ID (-79)
      0x85, 0xB2,                    //     Report ID (-78)
      0x85, 0xB3,                    //     Report ID (-77)
      0x85, 0xB4,                    //     Report ID (-76)
      0x85, 0xB5,                    //     Report ID (-75)
      0x85, 0xB6,                    //     Report ID (-74)
      0x85, 0xB7,                    //     Report ID (-73)
      0x85, 0xB8,                    //     Report ID (-72)
      0x85, 0xB9,                    //     Report ID (-71)
      0x85, 0xBA,                    //     Report ID (-70)
      0x85, 0xBB,                    //     Report ID (-69)
      0x85, 0xBC,                    //     Report ID (-68)
      0x85, 0xBD,                    //     Report ID (-67)
      0x85, 0xBE,                    //     Report ID (-66)
      0x85, 0xBF,                    //     Report ID (-65)
      0x85, 0xC0,                    //     Report ID (-64)
      0x85, 0xC1,                    //     Report ID (-63)
      0x85, 0xC2,                    //     Report ID (-62)
      0x85, 0xC3,                    //     Report ID (-61)
      0x85, 0xC4,                    //     Report ID (-60)
      0x85, 0xC5,                    //     Report ID (-59)
      0x85, 0xC6,                    //     Report ID (-58)
      0x85, 0xC7,                    //     Report ID (-57)
      0x85, 0xC8,                    //     Report ID (-56)
      0x85, 0xC9,                    //     Report ID (-55)
      0x85, 0xCA,                    //     Report ID (-54)
      0x85, 0xCB,                    //     Report ID (-53)
      0x85, 0xCC,                    //     Report ID (-52)
      0x85, 0xCD,                    //     Report ID (-51)
      0x85, 0xCE,                    //     Report ID (-50)
      0x85, 0xCF,                    //     Report ID (-49)
      0x85, 0xD0,                    //     Report ID (-48)
      0x85, 0xD1,                    //     Report ID (-47)
      0x85, 0xD2,                    //     Report ID (-46)
      0x85, 0xD3,                    //     Report ID (-45)
      0x85, 0xD4,                    //     Report ID (-44)
      0x85, 0xD5,                    //     Report ID (-43)
      0x85, 0xD6,                    //     Report ID (-42)
      0x85, 0xD7,                    //     Report ID (-41)
      0x85, 0xD8,                    //     Report ID (-40)
      0x85, 0xD9,                    //     Report ID (-39)
      0x85, 0xDA,                    //     Report ID (-38)
      0x85, 0xDB,                    //     Report ID (-37)
      0x85, 0xDC,                    //     Report ID (-36)
      0x85, 0xDD,                    //     Report ID (-35)
      0x85, 0xDE,                    //     Report ID (-34)
      0x85, 0xDF,                    //     Report ID (-33)
      0x85, 0xE0,                    //     Report ID (-32)
      0x85, 0xE1,                    //     Report ID (-31)
      0x85, 0xE2,                    //     Report ID (-30)
      0x85, 0xE3,                    //     Report ID (-29)
      0x85, 0xE4,                    //     Report ID (-28)
      0x85, 0xE5,                    //     Report ID (-27)
      0x85, 0xE6,                    //     Report ID (-26)
      0x85, 0xE7,                    //     Report ID (-25)
      0x85, 0xE8,                    //     Report ID (-24)
      0x85, 0xE9,                    //     Report ID (-23)
      0x85, 0xEA,                    //     Report ID (-22)
      0x85, 0xEB,                    //     Report ID (-21)
      0x85, 0xEC,                    //     Report ID (-20)
      0x85, 0xED,                    //     Report ID (-19)
      0x85, 0xEE,                    //     Report ID (-18)
      0x85, 0xEF,                    //     Report ID (-17)
      0x85, 0xF0,                    //     Report ID (-16)
      0x85, 0xF1,                    //     Report ID (-15)
      0x85, 0xF2,                    //     Report ID (-14)
      0x85, 0xF3,                    //     Report ID (-13)
      0x85, 0xF4,                    //     Report ID (-12)
      0x85, 0xF5,                    //     Report ID (-11)
      0x85, 0xF6,                    //     Report ID (-10)
      0x85, 0xF7,                    //     Report ID (-9)
      0x85, 0xF8,                    //     Report ID (-8)
      0x85, 0xF9,                    //     Report ID (-7)
      0x85, 0xFA,                    //     Report ID (-6)
      0x85, 0xFB,                    //     Report ID (-5)
      0x85, 0xFC,                    //     Report ID (-4)
      0x85, 0xFD,                    //     Report ID (-3)
      0x85, 0xFE,                    //     Report ID (-2)
      0xB5, 0xAA,                    //   Pop
      0xA1, 0x01,                    //   Collection (Application)
      0x95, 0x01,                    //   Report Count (1)
      0x75, 0x08,                    //   Report Size (8)
      0xB3, 0xAA, 0xBB, 0xCC, 0xDD,  //     Feature (Data,Var,Abs,Wrap,Linear,No Preferred State,No
                                     //     Null Position,Volatile,Buffered Bytes)
      0xC0,                          //   End Collection
      0xC0,                          // End Collection
  };

  hid::DeviceDescriptor* desc = nullptr;
  hid::ParseResult res = hid::ParseReportDescriptor(report_desc, sizeof(report_desc), &desc);
  ASSERT_EQ(res, hid::ParseResult::kParseOk);

  FreeDeviceDescriptor(desc);
}

TEST(HidDescriptorTest, LimitLargeReportAllocations) {
  const uint8_t report_desc[] = {
      0x96, 0x04, 0xff, 0x04,  // Report Count (65284, 0xFF04)

      // The remaining bytes are required to trigger the issue.
      0xa1, 0x01, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
      0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
  hid::DeviceDescriptor* desc = nullptr;
  hid::ParseResult res = hid::ParseReportDescriptor(report_desc, sizeof(report_desc), &desc);
  ASSERT_EQ(hid::ParseResult::kParseNoMemory, res);

  FreeDeviceDescriptor(desc);
}

// This fuzzer-discovered descriptor allocates too many reports.
TEST(HidDescriptorTest, LimitManyReportAllocations) {
  const uint8_t report_desc[] = {
      0x96, 0xA5, 0xDF,  // Report Count (-8283)
      0x08,              // Usage
      0xA1, 0x01,        // Collection (Application)
      0x08,              //   Usage
      0xA0,              //   Collection
      0x08,              //     Usage
      0xA0,              //     Collection
      0x08,              //       Usage
      0xA0,              //       Collection
      0x08,              //         Usage
      0xA1, 0x01,        //         Collection (Application)
      0xA0,              //           Collection
      0x08,              //             Usage
      0x90,              //             Output
      0x08,              //             Usage
      0x90,              //             Output
      0x08,              //             Usage
      0xA0,              //             Collection
      0x08,              //               Usage
      0xA0,              //               Collection
      0x08,              //                 Usage
      0x90,              //                 Output
      0x08,              //                 Usage
      0xA1, 0x01,        //                 Collection (Application)
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0x36, 0x36, 0xA5,  //                   Physical Minimum (-23242)
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0x44,              //                   Physical Maximum
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xB0,              //                   Feature
      0xA4,              //                   Push
      0x90,              //                     Output
      0x08,              //                     Usage
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0x44,              //                     Physical Maximum
      0xA0,              //                     Collection
      0x08,              //                       Usage
      0xA0,              //                       Collection
      0x08,              //                         Usage
      0xA1, 0x01,        //                         Collection (Application)
      0xA0,              //                           Collection
      0x08,              //                             Usage
      0x90,              //                             Output
      0x08,              //                             Usage
      0x90,              //                             Output
      0x08,              //                             Usage
      0xA0,              //                             Collection
      0x08,              //                               Usage
      0xA0,              //                               Collection
      0x08,              //                                 Usage
      0x90,              //                                 Output
      0x08,              //                                 Usage
      0xA1, 0x01,        //                                 Collection (Application)
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0x36, 0x36, 0xB0,  //                                   Physical Minimum (-20426)
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xB0,              //                                   Feature
      0xA4,              //                                   Push
      0x90,              //                                     Output
      0x08,              //                                     Usage
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum
      0x44,              //                                     Physical Maximum

      // 407 bytes
  };

  hid::DeviceDescriptor* desc = nullptr;
  hid::ParseResult res = hid::ParseReportDescriptor(report_desc, sizeof(report_desc), &desc);
  ASSERT_EQ(hid::ParseResult::kParseNoMemory, res);

  FreeDeviceDescriptor(desc);
}

// Discovered from fuzzing. This descriptor attempts to allocate a large amount of
// usages.
TEST(HidDescriptorTest, LimitManyReportAllocations2) {
  const uint8_t report_desc[] = {
      0x35, 0xE2,  // Physical Minimum (-30)
      0x46, 0x2F,
      0x95,        // Physical Maximum (-27345)
      0x85, 0x85,  // Report ID (-123)
      0x85, 0x85,  // Report ID (-123)
      0x85, 0x85,  // Report ID (-123)
      0x85, 0x85,  // Report ID (-123)
      0x85, 0x85,  // Report ID (-123)
      0x85, 0x04,  // Report ID (4)
      0x96, 0x96,
      0x96,        // Report Count (-26986)
      0x24,        // Logical Maximum
      0xA1, 0x01,  // Collection (Application)
      0x82, 0x3B,
      0x82,  //   Input (Const,Var,Abs,Wrap,Nonlinear,No Preferred State,No Null Position,Bit Field)
      0x82, 0x0A,
      0x0A,  //   Input (Data,Var,Abs,Wrap,Linear,Preferred State,No Null Position,Bit Field)
      0x0A, 0x85,
      0x04,  //   Usage (0x0485)
      0x96, 0x96,
      0x96,        //   Report Count (-26986)
      0x24,        //   Logical Maximum
      0xA1, 0x01,  //   Collection (Application)
      0x82, 0x3B,
      0x82,  //     Input (Const,Var,Abs,Wrap,Nonlinear,No Preferred State,No Null Position,Bit
             //     Field)
      0x82, 0x0A,
      0x0A,  //     Input (Data,Var,Abs,Wrap,Linear,Preferred State,No Null Position,Bit Field)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x00,
      0x0A,  //     Usage (0x0A00)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x5D,  //     Usage (0x5D0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,  //     Usage (0x050A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x0A,  //     Usage (0x0A05)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,        //     Usage (0x0A0A)
      0x05, 0x0A,  //     Usage Page (Ordinal)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,        //     Usage (0x0A0A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x29, 0x80,  //     Usage Maximum (0x80)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x0A,  //     Usage (0x0A05)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,        //     Usage (0x0A0A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x2A,
      0x0A,  //     Usage (0x0A2A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x29, 0x80,  //     Usage Maximum (0x80)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x0A,  //     Usage (0x0A05)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,        //     Usage (0x0A0A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x80,  //     Usage Page (Monitor Pages)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x29, 0x80,  //     Usage Maximum (0x80)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x0A,  //     Usage (0x0A05)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,        //     Usage (0x0A0A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x29, 0x80,  //     Usage Maximum (0x80)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x0A,  //     Usage (0x0A05)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,        //     Usage (0x0A0A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x80,  //     Usage Page (Monitor Pages)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x2A,
      0x0A,  //     Usage (0x0A2A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x29, 0x80,  //     Usage Maximum (0x80)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x0A,  //     Usage (0x0A05)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,        //     Usage (0x0A0A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x80,  //     Usage Page (Monitor Pages)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x80,  //     Usage Page (Monitor Pages)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x0A,  //     Usage Page (Ordinal)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x24,  //     Logical Maximum
      0x24,  //     Logical Maximum
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x2A,
      0x0A,  //     Usage (0x0A2A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x05,        //     Usage (0x050A)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x05,
      0x05,        //     Usage (0x0505)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x05, 0x05,  //     Usage Page (Game Ctrls)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x29, 0x80,  //     Usage Maximum (0x80)
      0x80,        //     Input
      0x80,        //     Input
      0x80,        //     Input
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x24,
      0x24,  //     Usage (0x2424)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x0A, 0x0A,
      0x0A,  //     Usage (0x0A0A)
      0x08,  //     Usage
      0x54,  //     Unit Exponent

      // 3586 bytes

  };

  hid::DeviceDescriptor* desc = nullptr;
  hid::ParseResult res = hid::ParseReportDescriptor(report_desc, sizeof(report_desc), &desc);
  ASSERT_EQ(hid::ParseResult::kParseNoMemory, res);

  FreeDeviceDescriptor(desc);
}
