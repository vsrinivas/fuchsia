// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lcd.h"

#include <lib/device-protocol/display-panel.h>
#include <lib/mipi-dsi/mipi-dsi.h>

#include <ddk/debug.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>

#include "common.h"

#define DELAY_CMD (0xFF)
#define DCS_CMD (0xFE)
#define GEN_CMD (0xFD)

#define READ_DISPLAY_ID_CMD (0x04)
#define READ_DISPLAY_ID_LEN (0x03)

namespace amlogic_display {

namespace {
// Based on Vendor datasheet
// <CMD TYPE><LENGTH><DATA...>
// <DELAY_CMD><DELAY (ms)>
constexpr uint8_t lcd_shutdown_sequence[] = {
    DELAY_CMD, 5, DCS_CMD, 1, 0x28, DELAY_CMD, 30, DCS_CMD, 1, 0x10, DELAY_CMD, 150,
};

constexpr uint8_t lcd_init_sequence_TV070WSM_FT[] = {
    GEN_CMD, 2,       0xE0, 0x00, GEN_CMD,   2,       0xE1, 0x93, GEN_CMD, 2,       0xE2, 0x65,
    GEN_CMD, 2,       0xE3, 0xF8, GEN_CMD,   2,       0xE0, 0x01, GEN_CMD, 2,       0x00, 0x00,
    GEN_CMD, 2,       0x01, 0x90, GEN_CMD,   2,       0x03, 0x00, GEN_CMD, 2,       0x04, 0x90,
    GEN_CMD, 2,       0x17, 0x00, GEN_CMD,   2,       0x18, 0xB0, GEN_CMD, 2,       0x19, 0x01,
    GEN_CMD, 2,       0x1A, 0x00, GEN_CMD,   2,       0x1B, 0xB0, GEN_CMD, 2,       0x1C, 0x01,
    GEN_CMD, 2,       0x1F, 0x3E, GEN_CMD,   2,       0x20, 0x2F, GEN_CMD, 2,       0x21, 0x2F,
    GEN_CMD, 2,       0x22, 0x0E, GEN_CMD,   2,       0x37, 0x69, GEN_CMD, 2,       0x38, 0x05,
    GEN_CMD, 2,       0x39, 0x00, GEN_CMD,   2,       0x3A, 0x01, GEN_CMD, 2,       0x3C, 0x90,
    GEN_CMD, 2,       0x3D, 0xFF, GEN_CMD,   2,       0x3E, 0xFF, GEN_CMD, 2,       0x3F, 0xFF,
    GEN_CMD, 2,       0x40, 0x02, GEN_CMD,   2,       0x41, 0x80, GEN_CMD, 2,       0x42, 0x99,
    GEN_CMD, 2,       0x43, 0x06, GEN_CMD,   2,       0x44, 0x09, GEN_CMD, 2,       0x45, 0x3C,
    GEN_CMD, 2,       0x4B, 0x04, GEN_CMD,   2,       0x55, 0x0D, GEN_CMD, 2,       0x56, 0x01,
    GEN_CMD, 2,       0x57, 0x89, GEN_CMD,   2,       0x58, 0x0A, GEN_CMD, 2,       0x59, 0x0A,
    GEN_CMD, 2,       0x5A, 0x27, GEN_CMD,   2,       0x5B, 0x15, GEN_CMD, 2,       0x5D, 0x7C,
    GEN_CMD, 2,       0x5E, 0x67, GEN_CMD,   2,       0x5F, 0x58, GEN_CMD, 2,       0x60, 0x4C,
    GEN_CMD, 2,       0x61, 0x48, GEN_CMD,   2,       0x62, 0x38, GEN_CMD, 2,       0x63, 0x3C,
    GEN_CMD, 2,       0x64, 0x24, GEN_CMD,   2,       0x65, 0x3B, GEN_CMD, 2,       0x66, 0x38,
    GEN_CMD, 2,       0x67, 0x36, GEN_CMD,   2,       0x68, 0x53, GEN_CMD, 2,       0x69, 0x3F,
    GEN_CMD, 2,       0x6A, 0x44, GEN_CMD,   2,       0x6B, 0x35, GEN_CMD, 2,       0x6C, 0x2E,
    GEN_CMD, 2,       0x6D, 0x1F, GEN_CMD,   2,       0x6E, 0x0C, GEN_CMD, 2,       0x6F, 0x00,
    GEN_CMD, 2,       0x70, 0x7C, GEN_CMD,   2,       0x71, 0x67, GEN_CMD, 2,       0x72, 0x58,
    GEN_CMD, 2,       0x73, 0x4C, GEN_CMD,   2,       0x74, 0x48, GEN_CMD, 2,       0x75, 0x38,
    GEN_CMD, 2,       0x76, 0x3C, GEN_CMD,   2,       0x77, 0x24, GEN_CMD, 2,       0x78, 0x3B,
    GEN_CMD, 2,       0x79, 0x38, GEN_CMD,   2,       0x7A, 0x36, GEN_CMD, 2,       0x7B, 0x53,
    GEN_CMD, 2,       0x7C, 0x3F, GEN_CMD,   2,       0x7D, 0x44, GEN_CMD, 2,       0x7E, 0x35,
    GEN_CMD, 2,       0x7F, 0x2E, GEN_CMD,   2,       0x80, 0x1F, GEN_CMD, 2,       0x81, 0x0C,
    GEN_CMD, 2,       0x82, 0x00, GEN_CMD,   2,       0xE0, 0x02, GEN_CMD, 2,       0x00, 0x45,
    GEN_CMD, 2,       0x01, 0x45, GEN_CMD,   2,       0x02, 0x47, GEN_CMD, 2,       0x03, 0x47,
    GEN_CMD, 2,       0x04, 0x41, GEN_CMD,   2,       0x05, 0x41, GEN_CMD, 2,       0x06, 0x1F,
    GEN_CMD, 2,       0x07, 0x1F, GEN_CMD,   2,       0x08, 0x1F, GEN_CMD, 2,       0x09, 0x1F,
    GEN_CMD, 2,       0x0A, 0x1F, GEN_CMD,   2,       0x0B, 0x1F, GEN_CMD, 2,       0x0C, 0x1F,
    GEN_CMD, 2,       0x0D, 0x1D, GEN_CMD,   2,       0x0E, 0x1D, GEN_CMD, 2,       0x0F, 0x1D,
    GEN_CMD, 2,       0x10, 0x1F, GEN_CMD,   2,       0x11, 0x1F, GEN_CMD, 2,       0x12, 0x1F,
    GEN_CMD, 2,       0x13, 0x1F, GEN_CMD,   2,       0x14, 0x1F, GEN_CMD, 2,       0x15, 0x1F,
    GEN_CMD, 2,       0x16, 0x44, GEN_CMD,   2,       0x17, 0x44, GEN_CMD, 2,       0x18, 0x46,
    GEN_CMD, 2,       0x19, 0x46, GEN_CMD,   2,       0x1A, 0x40, GEN_CMD, 2,       0x1B, 0x40,
    GEN_CMD, 2,       0x1C, 0x1F, GEN_CMD,   2,       0x1D, 0x1F, GEN_CMD, 2,       0x1E, 0x1F,
    GEN_CMD, 2,       0x1F, 0x1F, GEN_CMD,   2,       0x20, 0x1F, GEN_CMD, 2,       0x21, 0x1F,
    GEN_CMD, 2,       0x22, 0x1F, GEN_CMD,   2,       0x23, 0x1D, GEN_CMD, 2,       0x24, 0x1D,
    GEN_CMD, 2,       0x25, 0x1D, GEN_CMD,   2,       0x26, 0x1F, GEN_CMD, 2,       0x27, 0x1F,
    GEN_CMD, 2,       0x28, 0x1F, GEN_CMD,   2,       0x29, 0x1F, GEN_CMD, 2,       0x2A, 0x1F,
    GEN_CMD, 2,       0x2B, 0x1F, GEN_CMD,   2,       0x58, 0x40, GEN_CMD, 2,       0x59, 0x00,
    GEN_CMD, 2,       0x5A, 0x00, GEN_CMD,   2,       0x5B, 0x10, GEN_CMD, 2,       0x5C, 0x06,
    GEN_CMD, 2,       0x5D, 0x20, GEN_CMD,   2,       0x5E, 0x00, GEN_CMD, 2,       0x5F, 0x00,
    GEN_CMD, 2,       0x61, 0x00, GEN_CMD,   2,       0x62, 0x00, GEN_CMD, 2,       0x63, 0x7A,
    GEN_CMD, 2,       0x64, 0x7A, GEN_CMD,   2,       0x65, 0x00, GEN_CMD, 2,       0x66, 0x00,
    GEN_CMD, 2,       0x67, 0x32, GEN_CMD,   2,       0x68, 0x08, GEN_CMD, 2,       0x69, 0x7A,
    GEN_CMD, 2,       0x6A, 0x7A, GEN_CMD,   2,       0x6B, 0x00, GEN_CMD, 2,       0x6C, 0x00,
    GEN_CMD, 2,       0x6D, 0x04, GEN_CMD,   2,       0x6E, 0x04, GEN_CMD, 2,       0x6F, 0x88,
    GEN_CMD, 2,       0x70, 0x00, GEN_CMD,   2,       0x71, 0x00, GEN_CMD, 2,       0x72, 0x06,
    GEN_CMD, 2,       0x73, 0x7B, GEN_CMD,   2,       0x74, 0x00, GEN_CMD, 2,       0x75, 0x07,
    GEN_CMD, 2,       0x76, 0x00, GEN_CMD,   2,       0x77, 0x5D, GEN_CMD, 2,       0x78, 0x17,
    GEN_CMD, 2,       0x79, 0x1F, GEN_CMD,   2,       0x7A, 0x00, GEN_CMD, 2,       0x7B, 0x00,
    GEN_CMD, 2,       0x7C, 0x00, GEN_CMD,   2,       0x7D, 0x03, GEN_CMD, 2,       0x7E, 0x7B,
    GEN_CMD, 2,       0xE0, 0x03, GEN_CMD,   2,       0xAF, 0x20, GEN_CMD, 2,       0xE0, 0x04,
    GEN_CMD, 2,       0x09, 0x11, GEN_CMD,   2,       0x0E, 0x48, GEN_CMD, 2,       0x2B, 0x2B,
    GEN_CMD, 2,       0x2E, 0x44, GEN_CMD,   2,       0x41, 0xFF, GEN_CMD, 2,       0xE0, 0x00,
    GEN_CMD, 2,       0xE6, 0x02, GEN_CMD,   2,       0xE7, 0x0C, DCS_CMD, 1,       0x11, DELAY_CMD,
    120,     GEN_CMD, 2,    0xE0, 0x03,      GEN_CMD, 2,    0x2B, 0x01,    GEN_CMD, 2,    0x2C,
    0x00,    GEN_CMD, 2,    0x30, 0x03,      GEN_CMD, 2,    0x31, 0xCC,    GEN_CMD, 2,    0x32,
    0x03,    GEN_CMD, 2,    0x33, 0xC9,      GEN_CMD, 2,    0x34, 0x03,    GEN_CMD, 2,    0x35,
    0xC0,    GEN_CMD, 2,    0x36, 0x03,      GEN_CMD, 2,    0x37, 0xB3,    GEN_CMD, 2,    0x38,
    0x03,    GEN_CMD, 2,    0x39, 0xAB,      GEN_CMD, 2,    0x3A, 0x03,    GEN_CMD, 2,    0x3B,
    0x9D,    GEN_CMD, 2,    0x3C, 0x03,      GEN_CMD, 2,    0x3D, 0x8F,    GEN_CMD, 2,    0x3E,
    0x03,    GEN_CMD, 2,    0x3F, 0x6D,      GEN_CMD, 2,    0x40, 0x03,    GEN_CMD, 2,    0x41,
    0x51,    GEN_CMD, 2,    0x42, 0x03,      GEN_CMD, 2,    0x43, 0x17,    GEN_CMD, 2,    0x44,
    0x02,    GEN_CMD, 2,    0x45, 0xD8,      GEN_CMD, 2,    0x46, 0x02,    GEN_CMD, 2,    0x47,
    0x60,    GEN_CMD, 2,    0x48, 0x01,      GEN_CMD, 2,    0x49, 0xEB,    GEN_CMD, 2,    0x4A,
    0x01,    GEN_CMD, 2,    0x4B, 0xE5,      GEN_CMD, 2,    0x4C, 0x01,    GEN_CMD, 2,    0x4D,
    0x6C,    GEN_CMD, 2,    0x4E, 0x00,      GEN_CMD, 2,    0x4F, 0xF2,    GEN_CMD, 2,    0x50,
    0x00,    GEN_CMD, 2,    0x51, 0xB4,      GEN_CMD, 2,    0x52, 0x00,    GEN_CMD, 2,    0x53,
    0x74,    GEN_CMD, 2,    0x54, 0x00,      GEN_CMD, 2,    0x55, 0x54,    GEN_CMD, 2,    0x56,
    0x00,    GEN_CMD, 2,    0x57, 0x34,      GEN_CMD, 2,    0x58, 0x00,    GEN_CMD, 2,    0x59,
    0x26,    GEN_CMD, 2,    0x5A, 0x00,      GEN_CMD, 2,    0x5B, 0x18,    GEN_CMD, 2,    0x5C,
    0x00,    GEN_CMD, 2,    0x5D, 0x11,      GEN_CMD, 2,    0x5E, 0x00,    GEN_CMD, 2,    0x5F,
    0x0A,    GEN_CMD, 2,    0x60, 0x00,      GEN_CMD, 2,    0x61, 0x03,    GEN_CMD, 2,    0x62,
    0x00,    GEN_CMD, 2,    0x63, 0x00,      GEN_CMD, 2,    0x64, 0x03,    GEN_CMD, 2,    0x65,
    0x9E,    GEN_CMD, 2,    0x66, 0x03,      GEN_CMD, 2,    0x67, 0x9B,    GEN_CMD, 2,    0x68,
    0x03,    GEN_CMD, 2,    0x69, 0x94,      GEN_CMD, 2,    0x6A, 0x03,    GEN_CMD, 2,    0x6B,
    0x8C,    GEN_CMD, 2,    0x6C, 0x03,      GEN_CMD, 2,    0x6D, 0x85,    GEN_CMD, 2,    0x6E,
    0x03,    GEN_CMD, 2,    0x6F, 0x76,      GEN_CMD, 2,    0x70, 0x03,    GEN_CMD, 2,    0x71,
    0x67,    GEN_CMD, 2,    0x72, 0x03,      GEN_CMD, 2,    0x73, 0x4B,    GEN_CMD, 2,    0x74,
    0x03,    GEN_CMD, 2,    0x75, 0x2E,      GEN_CMD, 2,    0x76, 0x02,    GEN_CMD, 2,    0x77,
    0xF7,    GEN_CMD, 2,    0x78, 0x02,      GEN_CMD, 2,    0x79, 0xB8,    GEN_CMD, 2,    0x7A,
    0x02,    GEN_CMD, 2,    0x7B, 0x46,      GEN_CMD, 2,    0x7C, 0x01,    GEN_CMD, 2,    0x7D,
    0xD6,    GEN_CMD, 2,    0x7E, 0x01,      GEN_CMD, 2,    0x7F, 0xD0,    GEN_CMD, 2,    0x80,
    0x01,    GEN_CMD, 2,    0x81, 0x5C,      GEN_CMD, 2,    0x82, 0x00,    GEN_CMD, 2,    0x83,
    0xE7,    GEN_CMD, 2,    0x84, 0x00,      GEN_CMD, 2,    0x85, 0xAA,    GEN_CMD, 2,    0x86,
    0x00,    GEN_CMD, 2,    0x87, 0x74,      GEN_CMD, 2,    0x88, 0x00,    GEN_CMD, 2,    0x89,
    0x5A,    GEN_CMD, 2,    0x8A, 0x00,      GEN_CMD, 2,    0x8B, 0x3C,    GEN_CMD, 2,    0x8C,
    0x00,    GEN_CMD, 2,    0x8D, 0x2C,      GEN_CMD, 2,    0x8E, 0x00,    GEN_CMD, 2,    0x8F,
    0x1C,    GEN_CMD, 2,    0x90, 0x00,      GEN_CMD, 2,    0x91, 0x14,    GEN_CMD, 2,    0x92,
    0x00,    GEN_CMD, 2,    0x93, 0x0C,      GEN_CMD, 2,    0x94, 0x00,    GEN_CMD, 2,    0x95,
    0x04,    GEN_CMD, 2,    0x96, 0x00,      GEN_CMD, 2,    0x97, 0x00,    GEN_CMD, 2,    0xE0,
    0x00,    DCS_CMD, 1,    0x29, DELAY_CMD, 5,
};
constexpr uint8_t lcd_init_sequence_P070ACB_FT[] = {
    DELAY_CMD, 100,       GEN_CMD, 2,       0xE0, 0x00, GEN_CMD, 2, 0xE1, 0x93,      GEN_CMD, 2,
    0xE2,      0x65,      GEN_CMD, 2,       0xE3, 0xF8, GEN_CMD, 2, 0x80, 0x03,      GEN_CMD, 2,
    0xE0,      0x01,      GEN_CMD, 2,       0x0C, 0x74, GEN_CMD, 2, 0x17, 0x00,      GEN_CMD, 2,
    0x18,      0xEF,      GEN_CMD, 2,       0x19, 0x00, GEN_CMD, 2, 0x1A, 0x00,      GEN_CMD, 2,
    0x1B,      0xEF,      GEN_CMD, 2,       0x1C, 0x00, GEN_CMD, 2, 0x1F, 0x70,      GEN_CMD, 2,
    0x20,      0x2D,      GEN_CMD, 2,       0x21, 0x2D, GEN_CMD, 2, 0x22, 0x7E,      GEN_CMD, 2,
    0x26,      0xF3,      GEN_CMD, 2,       0x37, 0x09, GEN_CMD, 2, 0x38, 0x04,      GEN_CMD, 2,
    0x39,      0x00,      GEN_CMD, 2,       0x3A, 0x01, GEN_CMD, 2, 0x3C, 0x90,      GEN_CMD, 2,
    0x3D,      0xFF,      GEN_CMD, 2,       0x3E, 0xFF, GEN_CMD, 2, 0x3F, 0xFF,      GEN_CMD, 2,
    0x40,      0x02,      GEN_CMD, 2,       0x41, 0x80, GEN_CMD, 2, 0x42, 0x99,      GEN_CMD, 2,
    0x43,      0x14,      GEN_CMD, 2,       0x44, 0x19, GEN_CMD, 2, 0x45, 0x5A,      GEN_CMD, 2,
    0x4B,      0x04,      GEN_CMD, 2,       0x55, 0x02, GEN_CMD, 2, 0x56, 0x01,      GEN_CMD, 2,
    0x57,      0x69,      GEN_CMD, 2,       0x58, 0x0A, GEN_CMD, 2, 0x59, 0x0A,      GEN_CMD, 2,
    0x5A,      0x2E,      GEN_CMD, 2,       0x5B, 0x19, GEN_CMD, 2, 0x5C, 0x15,      GEN_CMD, 2,
    0x5D,      0x77,      GEN_CMD, 2,       0x5E, 0x56, GEN_CMD, 2, 0x5F, 0x45,      GEN_CMD, 2,
    0x60,      0x38,      GEN_CMD, 2,       0x61, 0x35, GEN_CMD, 2, 0x62, 0x27,      GEN_CMD, 2,
    0x63,      0x2D,      GEN_CMD, 2,       0x64, 0x18, GEN_CMD, 2, 0x65, 0x33,      GEN_CMD, 2,
    0x66,      0x34,      GEN_CMD, 2,       0x67, 0x35, GEN_CMD, 2, 0x68, 0x56,      GEN_CMD, 2,
    0x69,      0x45,      GEN_CMD, 2,       0x6A, 0x4F, GEN_CMD, 2, 0x6B, 0x42,      GEN_CMD, 2,
    0x6C,      0x40,      GEN_CMD, 2,       0x6D, 0x34, GEN_CMD, 2, 0x6E, 0x25,      GEN_CMD, 2,
    0x6F,      0x02,      GEN_CMD, 2,       0x70, 0x77, GEN_CMD, 2, 0x71, 0x56,      GEN_CMD, 2,
    0x72,      0x45,      GEN_CMD, 2,       0x73, 0x38, GEN_CMD, 2, 0x74, 0x35,      GEN_CMD, 2,
    0x75,      0x27,      GEN_CMD, 2,       0x76, 0x2D, GEN_CMD, 2, 0x77, 0x18,      GEN_CMD, 2,
    0x78,      0x33,      GEN_CMD, 2,       0x79, 0x34, GEN_CMD, 2, 0x7A, 0x35,      GEN_CMD, 2,
    0x7B,      0x56,      GEN_CMD, 2,       0x7C, 0x45, GEN_CMD, 2, 0x7D, 0x4F,      GEN_CMD, 2,
    0x7E,      0x42,      GEN_CMD, 2,       0x7F, 0x40, GEN_CMD, 2, 0x80, 0x34,      GEN_CMD, 2,
    0x81,      0x25,      GEN_CMD, 2,       0x82, 0x02, GEN_CMD, 2, 0xE0, 0x02,      GEN_CMD, 2,
    0x00,      0x53,      GEN_CMD, 2,       0x01, 0x55, GEN_CMD, 2, 0x02, 0x55,      GEN_CMD, 2,
    0x03,      0x51,      GEN_CMD, 2,       0x04, 0x77, GEN_CMD, 2, 0x05, 0x57,      GEN_CMD, 2,
    0x06,      0x1F,      GEN_CMD, 2,       0x07, 0x4F, GEN_CMD, 2, 0x08, 0x4D,      GEN_CMD, 2,
    0x09,      0x1F,      GEN_CMD, 2,       0x0A, 0x4B, GEN_CMD, 2, 0x0B, 0x49,      GEN_CMD, 2,
    0x0C,      0x1F,      GEN_CMD, 2,       0x0D, 0x47, GEN_CMD, 2, 0x0E, 0x45,      GEN_CMD, 2,
    0x0F,      0x41,      GEN_CMD, 2,       0x10, 0x1F, GEN_CMD, 2, 0x11, 0x1F,      GEN_CMD, 2,
    0x12,      0x1F,      GEN_CMD, 2,       0x13, 0x55, GEN_CMD, 2, 0x14, 0x1F,      GEN_CMD, 2,
    0x15,      0x1F,      GEN_CMD, 2,       0x16, 0x52, GEN_CMD, 2, 0x17, 0x55,      GEN_CMD, 2,
    0x18,      0x55,      GEN_CMD, 2,       0x19, 0x50, GEN_CMD, 2, 0x1A, 0x77,      GEN_CMD, 2,
    0x1B,      0x57,      GEN_CMD, 2,       0x1C, 0x1F, GEN_CMD, 2, 0x1D, 0x4E,      GEN_CMD, 2,
    0x1E,      0x4C,      GEN_CMD, 2,       0x1F, 0x1F, GEN_CMD, 2, 0x20, 0x4A,      GEN_CMD, 2,
    0x21,      0x48,      GEN_CMD, 2,       0x22, 0x1F, GEN_CMD, 2, 0x23, 0x46,      GEN_CMD, 2,
    0x24,      0x44,      GEN_CMD, 2,       0x25, 0x40, GEN_CMD, 2, 0x26, 0x1F,      GEN_CMD, 2,
    0x27,      0x1F,      GEN_CMD, 2,       0x28, 0x1F, GEN_CMD, 2, 0x29, 0x1F,      GEN_CMD, 2,
    0x2A,      0x1F,      GEN_CMD, 2,       0x2B, 0x55, GEN_CMD, 2, 0x2C, 0x12,      GEN_CMD, 2,
    0x2D,      0x15,      GEN_CMD, 2,       0x2E, 0x15, GEN_CMD, 2, 0x2F, 0x00,      GEN_CMD, 2,
    0x30,      0x37,      GEN_CMD, 2,       0x31, 0x17, GEN_CMD, 2, 0x32, 0x1F,      GEN_CMD, 2,
    0x33,      0x08,      GEN_CMD, 2,       0x34, 0x0A, GEN_CMD, 2, 0x35, 0x1F,      GEN_CMD, 2,
    0x36,      0x0C,      GEN_CMD, 2,       0x37, 0x0E, GEN_CMD, 2, 0x38, 0x1F,      GEN_CMD, 2,
    0x39,      0x04,      GEN_CMD, 2,       0x3A, 0x06, GEN_CMD, 2, 0x3B, 0x10,      GEN_CMD, 2,
    0x3C,      0x1F,      GEN_CMD, 2,       0x3D, 0x1F, GEN_CMD, 2, 0x3E, 0x1F,      GEN_CMD, 2,
    0x3F,      0x15,      GEN_CMD, 2,       0x40, 0x1F, GEN_CMD, 2, 0x41, 0x1F,      GEN_CMD, 2,
    0x42,      0x13,      GEN_CMD, 2,       0x43, 0x15, GEN_CMD, 2, 0x44, 0x15,      GEN_CMD, 2,
    0x45,      0x01,      GEN_CMD, 2,       0x46, 0x37, GEN_CMD, 2, 0x47, 0x17,      GEN_CMD, 2,
    0x48,      0x1F,      GEN_CMD, 2,       0x49, 0x09, GEN_CMD, 2, 0x4A, 0x0B,      GEN_CMD, 2,
    0x4B,      0x1F,      GEN_CMD, 2,       0x4C, 0x0D, GEN_CMD, 2, 0x4D, 0x0F,      GEN_CMD, 2,
    0x4E,      0x1F,      GEN_CMD, 2,       0x4F, 0x05, GEN_CMD, 2, 0x50, 0x07,      GEN_CMD, 2,
    0x51,      0x11,      GEN_CMD, 2,       0x52, 0x1F, GEN_CMD, 2, 0x53, 0x1F,      GEN_CMD, 2,
    0x54,      0x1F,      GEN_CMD, 2,       0x55, 0x1F, GEN_CMD, 2, 0x56, 0x1F,      GEN_CMD, 2,
    0x57,      0x15,      GEN_CMD, 2,       0x58, 0x40, GEN_CMD, 2, 0x59, 0x00,      GEN_CMD, 2,
    0x5A,      0x00,      GEN_CMD, 2,       0x5B, 0x10, GEN_CMD, 2, 0x5C, 0x14,      GEN_CMD, 2,
    0x5D,      0x40,      GEN_CMD, 2,       0x5E, 0x01, GEN_CMD, 2, 0x5F, 0x02,      GEN_CMD, 2,
    0x60,      0x40,      GEN_CMD, 2,       0x61, 0x03, GEN_CMD, 2, 0x62, 0x04,      GEN_CMD, 2,
    0x63,      0x7A,      GEN_CMD, 2,       0x64, 0x7A, GEN_CMD, 2, 0x65, 0x74,      GEN_CMD, 2,
    0x66,      0x16,      GEN_CMD, 2,       0x67, 0xB4, GEN_CMD, 2, 0x68, 0x16,      GEN_CMD, 2,
    0x69,      0x7A,      GEN_CMD, 2,       0x6A, 0x7A, GEN_CMD, 2, 0x6B, 0x0C,      GEN_CMD, 2,
    0x6C,      0x00,      GEN_CMD, 2,       0x6D, 0x04, GEN_CMD, 2, 0x6E, 0x04,      GEN_CMD, 2,
    0x6F,      0x88,      GEN_CMD, 2,       0x70, 0x00, GEN_CMD, 2, 0x71, 0x00,      GEN_CMD, 2,
    0x72,      0x06,      GEN_CMD, 2,       0x73, 0x7B, GEN_CMD, 2, 0x74, 0x00,      GEN_CMD, 2,
    0x75,      0xBC,      GEN_CMD, 2,       0x76, 0x00, GEN_CMD, 2, 0x77, 0x04,      GEN_CMD, 2,
    0x78,      0x2C,      GEN_CMD, 2,       0x79, 0x00, GEN_CMD, 2, 0x7A, 0x00,      GEN_CMD, 2,
    0x7B,      0x00,      GEN_CMD, 2,       0x7C, 0x00, GEN_CMD, 2, 0x7D, 0x03,      GEN_CMD, 2,
    0x7E,      0x7B,      GEN_CMD, 2,       0xE0, 0x04, GEN_CMD, 2, 0x09, 0x11,      GEN_CMD, 2,
    0x0E,      0x48,      GEN_CMD, 2,       0x2B, 0x2B, GEN_CMD, 2, 0x2E, 0x44,      GEN_CMD, 2,
    0xE0,      0x00,      GEN_CMD, 2,       0xE6, 0x02, GEN_CMD, 2, 0xE7, 0x0C,      DCS_CMD, 1,
    0x11,      DELAY_CMD, 120,     DCS_CMD, 1,    0x29, DCS_CMD, 1, 0x35, DELAY_CMD, 20,
};
constexpr uint8_t lcd_init_sequence_G101B158_FT[] = {
    GEN_CMD,   2,         0xE0,    0x00,    GEN_CMD, 2,       0xE1,    0x93,    GEN_CMD, 2,
    0xE2,      0x65,      GEN_CMD, 2,       0xE3,    0xF8,    GEN_CMD, 2,       0x80,    0x03,
    GEN_CMD,   2,         0xE0,    0x01,    GEN_CMD, 2,       0x00,    0x00,    GEN_CMD, 2,
    0x01,      0x5D,      GEN_CMD, 2,       0x03,    0x00,    GEN_CMD, 2,       0x04,    0x64,
    GEN_CMD,   2,         0x17,    0x00,    GEN_CMD, 2,       0x18,    0xC7,    GEN_CMD, 2,
    0x19,      0x01,      GEN_CMD, 2,       0x1A,    0x00,    GEN_CMD, 2,       0x1B,    0xC7,
    GEN_CMD,   2,         0x1C,    0x01,    GEN_CMD, 2,       0x1F,    0x70,    GEN_CMD, 2,
    0x20,      0x2D,      GEN_CMD, 2,       0x21,    0x2D,    GEN_CMD, 2,       0x22,    0x7E,
    GEN_CMD,   2,         0x35,    0x28,    GEN_CMD, 2,       0x37,    0x19,    GEN_CMD, 2,
    0x38,      0x05,      GEN_CMD, 2,       0x39,    0x00,    GEN_CMD, 2,       0x3A,    0x01,
    GEN_CMD,   2,         0x3C,    0x7C,    GEN_CMD, 2,       0x3D,    0xFF,    GEN_CMD, 2,
    0x3E,      0xFF,      GEN_CMD, 2,       0x3F,    0x7F,    GEN_CMD, 2,       0x40,    0x06,
    GEN_CMD,   2,         0x41,    0xA0,    GEN_CMD, 2,       0x43,    0x14,    GEN_CMD, 2,
    0x44,      0x17,      GEN_CMD, 2,       0x45,    0x2C,    GEN_CMD, 2,       0x55,    0x0F,
    GEN_CMD,   2,         0x57,    0x68,    GEN_CMD, 2,       0x59,    0x0A,    GEN_CMD, 2,
    0x5A,      0x2E,      GEN_CMD, 2,       0x5B,    0x1A,    GEN_CMD, 2,       0x5C,    0x15,
    GEN_CMD,   2,         0x5D,    0x7F,    GEN_CMD, 2,       0x5E,    0x61,    GEN_CMD, 2,
    0x5F,      0x50,      GEN_CMD, 2,       0x60,    0x43,    GEN_CMD, 2,       0x61,    0x3E,
    GEN_CMD,   2,         0x62,    0x2E,    GEN_CMD, 2,       0x63,    0x33,    GEN_CMD, 2,
    0x64,      0x1C,      GEN_CMD, 2,       0x65,    0x34,    GEN_CMD, 2,       0x66,    0x33,
    GEN_CMD,   2,         0x67,    0x32,    GEN_CMD, 2,       0x68,    0x50,    GEN_CMD, 2,
    0x69,      0x3E,      GEN_CMD, 2,       0x6A,    0x46,    GEN_CMD, 2,       0x6B,    0x37,
    GEN_CMD,   2,         0x6C,    0x32,    GEN_CMD, 2,       0x6D,    0x24,    GEN_CMD, 2,
    0x6E,      0x12,      GEN_CMD, 2,       0x6F,    0x02,    GEN_CMD, 2,       0x70,    0x7F,
    GEN_CMD,   2,         0x71,    0x61,    GEN_CMD, 2,       0x72,    0x50,    GEN_CMD, 2,
    0x73,      0x43,      GEN_CMD, 2,       0x74,    0x3E,    GEN_CMD, 2,       0x75,    0x2E,
    GEN_CMD,   2,         0x76,    0x33,    GEN_CMD, 2,       0x77,    0x1C,    GEN_CMD, 2,
    0x78,      0x34,      GEN_CMD, 2,       0x79,    0x33,    GEN_CMD, 2,       0x7A,    0x32,
    GEN_CMD,   2,         0x7B,    0x50,    GEN_CMD, 2,       0x7C,    0x3E,    GEN_CMD, 2,
    0x7D,      0x46,      GEN_CMD, 2,       0x7E,    0x37,    GEN_CMD, 2,       0x7F,    0x32,
    GEN_CMD,   2,         0x80,    0x24,    GEN_CMD, 2,       0x81,    0x12,    GEN_CMD, 2,
    0x82,      0x02,      GEN_CMD, 2,       0xE0,    0x02,    GEN_CMD, 2,       0x00,    0x52,
    GEN_CMD,   2,         0x01,    0x55,    GEN_CMD, 2,       0x02,    0x55,    GEN_CMD, 2,
    0x03,      0x50,      GEN_CMD, 2,       0x04,    0x77,    GEN_CMD, 2,       0x05,    0x57,
    GEN_CMD,   2,         0x06,    0x55,    GEN_CMD, 2,       0x07,    0x4E,    GEN_CMD, 2,
    0x08,      0x4C,      GEN_CMD, 2,       0x09,    0x5F,    GEN_CMD, 2,       0x0A,    0x4A,
    GEN_CMD,   2,         0x0B,    0x48,    GEN_CMD, 2,       0x0C,    0x55,    GEN_CMD, 2,
    0x0D,      0x46,      GEN_CMD, 2,       0x0E,    0x44,    GEN_CMD, 2,       0x0F,    0x40,
    GEN_CMD,   2,         0x10,    0x55,    GEN_CMD, 2,       0x11,    0x55,    GEN_CMD, 2,
    0x12,      0x55,      GEN_CMD, 2,       0x13,    0x55,    GEN_CMD, 2,       0x14,    0x55,
    GEN_CMD,   2,         0x15,    0x55,    GEN_CMD, 2,       0x16,    0x53,    GEN_CMD, 2,
    0x17,      0x55,      GEN_CMD, 2,       0x18,    0x55,    GEN_CMD, 2,       0x19,    0x51,
    GEN_CMD,   2,         0x1A,    0x77,    GEN_CMD, 2,       0x1B,    0x57,    GEN_CMD, 2,
    0x1C,      0x55,      GEN_CMD, 2,       0x1D,    0x4F,    GEN_CMD, 2,       0x1E,    0x4D,
    GEN_CMD,   2,         0x1F,    0x5F,    GEN_CMD, 2,       0x20,    0x4B,    GEN_CMD, 2,
    0x21,      0x49,      GEN_CMD, 2,       0x22,    0x55,    GEN_CMD, 2,       0x23,    0x47,
    GEN_CMD,   2,         0x24,    0x45,    GEN_CMD, 2,       0x25,    0x41,    GEN_CMD, 2,
    0x26,      0x55,      GEN_CMD, 2,       0x27,    0x55,    GEN_CMD, 2,       0x28,    0x55,
    GEN_CMD,   2,         0x29,    0x55,    GEN_CMD, 2,       0x2A,    0x55,    GEN_CMD, 2,
    0x2B,      0x55,      GEN_CMD, 2,       0x2C,    0x13,    GEN_CMD, 2,       0x2D,    0x15,
    GEN_CMD,   2,         0x2E,    0x15,    GEN_CMD, 2,       0x2F,    0x01,    GEN_CMD, 2,
    0x30,      0x37,      GEN_CMD, 2,       0x31,    0x17,    GEN_CMD, 2,       0x32,    0x15,
    GEN_CMD,   2,         0x33,    0x0D,    GEN_CMD, 2,       0x34,    0x0F,    GEN_CMD, 2,
    0x35,      0x15,      GEN_CMD, 2,       0x36,    0x05,    GEN_CMD, 2,       0x37,    0x07,
    GEN_CMD,   2,         0x38,    0x15,    GEN_CMD, 2,       0x39,    0x09,    GEN_CMD, 2,
    0x3A,      0x0B,      GEN_CMD, 2,       0x3B,    0x11,    GEN_CMD, 2,       0x3C,    0x15,
    GEN_CMD,   2,         0x3D,    0x15,    GEN_CMD, 2,       0x3E,    0x15,    GEN_CMD, 2,
    0x3F,      0x15,      GEN_CMD, 2,       0x40,    0x15,    GEN_CMD, 2,       0x41,    0x15,
    GEN_CMD,   2,         0x42,    0x12,    GEN_CMD, 2,       0x43,    0x15,    GEN_CMD, 2,
    0x44,      0x15,      GEN_CMD, 2,       0x45,    0x00,    GEN_CMD, 2,       0x46,    0x37,
    GEN_CMD,   2,         0x47,    0x17,    GEN_CMD, 2,       0x48,    0x15,    GEN_CMD, 2,
    0x49,      0x0C,      GEN_CMD, 2,       0x4A,    0x0E,    GEN_CMD, 2,       0x4B,    0x15,
    GEN_CMD,   2,         0x4C,    0x04,    GEN_CMD, 2,       0x4D,    0x06,    GEN_CMD, 2,
    0x4E,      0x15,      GEN_CMD, 2,       0x4F,    0x08,    GEN_CMD, 2,       0x50,    0x0A,
    GEN_CMD,   2,         0x51,    0x10,    GEN_CMD, 2,       0x52,    0x15,    GEN_CMD, 2,
    0x53,      0x15,      GEN_CMD, 2,       0x54,    0x15,    GEN_CMD, 2,       0x55,    0x15,
    GEN_CMD,   2,         0x56,    0x15,    GEN_CMD, 2,       0x57,    0x15,    GEN_CMD, 2,
    0x58,      0x40,      GEN_CMD, 2,       0x5B,    0x10,    GEN_CMD, 2,       0x5C,    0x12,
    GEN_CMD,   2,         0x5D,    0x40,    GEN_CMD, 2,       0x5E,    0x00,    GEN_CMD, 2,
    0x5F,      0x00,      GEN_CMD, 2,       0x60,    0x40,    GEN_CMD, 2,       0x61,    0x03,
    GEN_CMD,   2,         0x62,    0x04,    GEN_CMD, 2,       0x63,    0x6C,    GEN_CMD, 2,
    0x64,      0x6C,      GEN_CMD, 2,       0x65,    0x75,    GEN_CMD, 2,       0x66,    0x14,
    GEN_CMD,   2,         0x67,    0xB4,    GEN_CMD, 2,       0x68,    0x14,    GEN_CMD, 2,
    0x69,      0x6C,      GEN_CMD, 2,       0x6A,    0x6C,    GEN_CMD, 2,       0x6B,    0x0C,
    GEN_CMD,   2,         0x6D,    0x04,    GEN_CMD, 2,       0x6E,    0x00,    GEN_CMD, 2,
    0x6F,      0x88,      GEN_CMD, 2,       0x75,    0xBB,    GEN_CMD, 2,       0x76,    0x02,
    GEN_CMD,   2,         0x77,    0x00,    GEN_CMD, 2,       0x78,    0x02,    GEN_CMD, 2,
    0xE0,      0x03,      GEN_CMD, 2,       0xAF,    0x20,    GEN_CMD, 2,       0xE0,    0x04,
    GEN_CMD,   2,         0x09,    0x11,    GEN_CMD, 2,       0x0E,    0x48,    GEN_CMD, 2,
    0x2B,      0x2B,      GEN_CMD, 2,       0x2D,    0x03,    GEN_CMD, 2,       0x2E,    0x44,
    GEN_CMD,   2,         0x41,    0xFF,    GEN_CMD, 2,       0xE0,    0x05,    GEN_CMD, 2,
    0x12,      0x72,      GEN_CMD, 2,       0xE0,    0x00,    GEN_CMD, 2,       0xE6,    0x02,
    GEN_CMD,   2,         0xE7,    0x0C,    GEN_CMD, 2,       0x53,    0x2C,    DCS_CMD, 1,
    0x11,      DELAY_CMD, 120,     GEN_CMD, 2,       0xE0,    0x03,    GEN_CMD, 2,       0x2B,
    0x01,      DELAY_CMD, 10,      GEN_CMD, 2,       0x2C,    0x01,    GEN_CMD, 2,       0x30,
    0x03,      GEN_CMD,   2,       0x31,    0xDE,    GEN_CMD, 2,       0x32,    0x03,    GEN_CMD,
    2,         0x33,      0xDA,    GEN_CMD, 2,       0x34,    0x03,    GEN_CMD, 2,       0x35,
    0xD1,      GEN_CMD,   2,       0x36,    0x03,    GEN_CMD, 2,       0x37,    0xC9,    GEN_CMD,
    2,         0x38,      0x03,    GEN_CMD, 2,       0x39,    0xC1,    GEN_CMD, 2,       0x3A,
    0x03,      GEN_CMD,   2,       0x3B,    0xB3,    GEN_CMD, 2,       0x3C,    0x03,    GEN_CMD,
    2,         0x3D,      0xA4,    GEN_CMD, 2,       0x3E,    0x03,    GEN_CMD, 2,       0x3F,
    0x83,      GEN_CMD,   2,       0x40,    0x03,    GEN_CMD, 2,       0x41,    0x62,    GEN_CMD,
    2,         0x42,      0x03,    GEN_CMD, 2,       0x43,    0x23,    GEN_CMD, 2,       0x44,
    0x02,      GEN_CMD,   2,       0x45,    0xE4,    GEN_CMD, 2,       0x46,    0x02,    GEN_CMD,
    2,         0x47,      0x67,    GEN_CMD, 2,       0x48,    0x01,    GEN_CMD, 2,       0x49,
    0xEC,      GEN_CMD,   2,       0x4A,    0x01,    GEN_CMD, 2,       0x4B,    0xE8,    GEN_CMD,
    2,         0x4C,      0x01,    GEN_CMD, 2,       0x4D,    0x6D,    GEN_CMD, 2,       0x4E,
    0x00,      GEN_CMD,   2,       0x4F,    0xF2,    GEN_CMD, 2,       0x50,    0x00,    GEN_CMD,
    2,         0x51,      0xB2,    GEN_CMD, 2,       0x52,    0x00,    GEN_CMD, 2,       0x53,
    0x76,      GEN_CMD,   2,       0x54,    0x00,    GEN_CMD, 2,       0x55,    0x58,    GEN_CMD,
    2,         0x56,      0x00,    GEN_CMD, 2,       0x57,    0x39,    GEN_CMD, 2,       0x58,
    0x00,      GEN_CMD,   2,       0x59,    0x2A,    GEN_CMD, 2,       0x5A,    0x00,    GEN_CMD,
    2,         0x5B,      0x1B,    GEN_CMD, 2,       0x5C,    0x00,    GEN_CMD, 2,       0x5D,
    0x13,      GEN_CMD,   2,       0x5E,    0x00,    GEN_CMD, 2,       0x5F,    0x0B,    GEN_CMD,
    2,         0x60,      0x00,    GEN_CMD, 2,       0x61,    0x04,    GEN_CMD, 2,       0x62,
    0x00,      GEN_CMD,   2,       0x63,    0x00,    GEN_CMD, 2,       0x64,    0x03,    GEN_CMD,
    2,         0x65,      0xE7,    GEN_CMD, 2,       0x66,    0x03,    GEN_CMD, 2,       0x67,
    0xE4,      GEN_CMD,   2,       0x68,    0x03,    GEN_CMD, 2,       0x69,    0xDD,    GEN_CMD,
    2,         0x6A,      0x03,    GEN_CMD, 2,       0x6B,    0xD5,    GEN_CMD, 2,       0x6C,
    0x03,      GEN_CMD,   2,       0x6D,    0xCE,    GEN_CMD, 2,       0x6E,    0x03,    GEN_CMD,
    2,         0x6F,      0xBF,    GEN_CMD, 2,       0x70,    0x03,    GEN_CMD, 2,       0x71,
    0xB2,      GEN_CMD,   2,       0x72,    0x03,    GEN_CMD, 2,       0x73,    0x93,    GEN_CMD,
    2,         0x74,      0x03,    GEN_CMD, 2,       0x75,    0x71,    GEN_CMD, 2,       0x76,
    0x03,      GEN_CMD,   2,       0x77,    0x33,    GEN_CMD, 2,       0x78,    0x02,    GEN_CMD,
    2,         0x79,      0xF4,    GEN_CMD, 2,       0x7A,    0x02,    GEN_CMD, 2,       0x7B,
    0x75,      GEN_CMD,   2,       0x7C,    0x01,    GEN_CMD, 2,       0x7D,    0xF7,    GEN_CMD,
    2,         0x7E,      0x01,    GEN_CMD, 2,       0x7F,    0xF3,    GEN_CMD, 2,       0x80,
    0x01,      GEN_CMD,   2,       0x81,    0x75,    GEN_CMD, 2,       0x82,    0x00,    GEN_CMD,
    2,         0x83,      0xF7,    GEN_CMD, 2,       0x84,    0x00,    GEN_CMD, 2,       0x85,
    0xB6,      GEN_CMD,   2,       0x86,    0x00,    GEN_CMD, 2,       0x87,    0x7C,    GEN_CMD,
    2,         0x88,      0x00,    GEN_CMD, 2,       0x89,    0x5E,    GEN_CMD, 2,       0x8A,
    0x00,      GEN_CMD,   2,       0x8B,    0x3F,    GEN_CMD, 2,       0x8C,    0x00,    GEN_CMD,
    2,         0x8D,      0x2E,    GEN_CMD, 2,       0x8E,    0x00,    GEN_CMD, 2,       0x8F,
    0x1D,      GEN_CMD,   2,       0x90,    0x00,    GEN_CMD, 2,       0x91,    0x15,    GEN_CMD,
    2,         0x92,      0x00,    GEN_CMD, 2,       0x93,    0x0C,    GEN_CMD, 2,       0x94,
    0x00,      GEN_CMD,   2,       0x95,    0x04,    GEN_CMD, 2,       0x96,    0x00,    GEN_CMD,
    2,         0x97,      0x00,    GEN_CMD, 2,       0xE0,    0x00,    DCS_CMD, 1,       0x29,
    DELAY_CMD, 5,
};
constexpr uint8_t lcd_init_sequence_TV101WXM_FT[] = {
    DELAY_CMD, 100,     GEN_CMD, 2,         0xE0,    0x00,    GEN_CMD, 2,       0xE1,    0x93,
    GEN_CMD,   2,       0xE2,    0x65,      GEN_CMD, 2,       0xE3,    0xF8,    GEN_CMD, 2,
    0x80,      0x03,    GEN_CMD, 2,         0xE0,    0x01,    GEN_CMD, 2,       0x00,    0x00,
    GEN_CMD,   2,       0x01,    0x55,      GEN_CMD, 2,       0x17,    0x00,    GEN_CMD, 2,
    0x18,      0xAF,    GEN_CMD, 2,         0x19,    0x01,    GEN_CMD, 2,       0x1A,    0x00,
    GEN_CMD,   2,       0x1B,    0xAF,      GEN_CMD, 2,       0x1C,    0x01,    GEN_CMD, 2,
    0x1F,      0x3E,    GEN_CMD, 2,         0x20,    0x28,    GEN_CMD, 2,       0x21,    0x28,
    GEN_CMD,   2,       0x22,    0x7E,      GEN_CMD, 2,       0x35,    0x26,    GEN_CMD, 2,
    0x37,      0x09,    GEN_CMD, 2,         0x38,    0x04,    GEN_CMD, 2,       0x39,    0x00,
    GEN_CMD,   2,       0x3A,    0x01,      GEN_CMD, 2,       0x3C,    0x78,    GEN_CMD, 2,
    0x3D,      0xFF,    GEN_CMD, 2,         0x3E,    0xFF,    GEN_CMD, 2,       0x3F,    0x7F,
    GEN_CMD,   2,       0x40,    0x06,      GEN_CMD, 2,       0x41,    0xA0,    GEN_CMD, 2,
    0x42,      0x81,    GEN_CMD, 2,         0x43,    0x08,    GEN_CMD, 2,       0x44,    0x0B,
    GEN_CMD,   2,       0x45,    0x28,      GEN_CMD, 2,       0x55,    0x0F,    GEN_CMD, 2,
    0x57,      0x69,    GEN_CMD, 2,         0x59,    0x0A,    GEN_CMD, 2,       0x5A,    0x28,
    GEN_CMD,   2,       0x5B,    0x14,      GEN_CMD, 2,       0x5D,    0x7F,    GEN_CMD, 2,
    0x5E,      0x67,    GEN_CMD, 2,         0x5F,    0x57,    GEN_CMD, 2,       0x60,    0x4A,
    GEN_CMD,   2,       0x61,    0x44,      GEN_CMD, 2,       0x62,    0x34,    GEN_CMD, 2,
    0x63,      0x37,    GEN_CMD, 2,         0x64,    0x1F,    GEN_CMD, 2,       0x65,    0x36,
    GEN_CMD,   2,       0x66,    0x33,      GEN_CMD, 2,       0x67,    0x32,    GEN_CMD, 2,
    0x68,      0x4F,    GEN_CMD, 2,         0x69,    0x3D,    GEN_CMD, 2,       0x6A,    0x47,
    GEN_CMD,   2,       0x6B,    0x38,      GEN_CMD, 2,       0x6C,    0x33,    GEN_CMD, 2,
    0x6D,      0x26,    GEN_CMD, 2,         0x6E,    0x13,    GEN_CMD, 2,       0x6F,    0x00,
    GEN_CMD,   2,       0x70,    0x7F,      GEN_CMD, 2,       0x71,    0x67,    GEN_CMD, 2,
    0x72,      0x57,    GEN_CMD, 2,         0x73,    0x4A,    GEN_CMD, 2,       0x74,    0x44,
    GEN_CMD,   2,       0x75,    0x34,      GEN_CMD, 2,       0x76,    0x37,    GEN_CMD, 2,
    0x77,      0x1F,    GEN_CMD, 2,         0x78,    0x36,    GEN_CMD, 2,       0x79,    0x33,
    GEN_CMD,   2,       0x7A,    0x32,      GEN_CMD, 2,       0x7B,    0x4F,    GEN_CMD, 2,
    0x7C,      0x3D,    GEN_CMD, 2,         0x7D,    0x47,    GEN_CMD, 2,       0x7E,    0x38,
    GEN_CMD,   2,       0x7F,    0x33,      GEN_CMD, 2,       0x80,    0x26,    GEN_CMD, 2,
    0x81,      0x13,    GEN_CMD, 2,         0x82,    0x00,    GEN_CMD, 2,       0xE0,    0x02,
    GEN_CMD,   2,       0x00,    0x1E,      GEN_CMD, 2,       0x01,    0x1E,    GEN_CMD, 2,
    0x02,      0x41,    GEN_CMD, 2,         0x03,    0x41,    GEN_CMD, 2,       0x04,    0x43,
    GEN_CMD,   2,       0x05,    0x43,      GEN_CMD, 2,       0x06,    0x1F,    GEN_CMD, 2,
    0x07,      0x1F,    GEN_CMD, 2,         0x08,    0x35,    GEN_CMD, 2,       0x09,    0x1F,
    GEN_CMD,   2,       0x0A,    0x15,      GEN_CMD, 2,       0x0B,    0x15,    GEN_CMD, 2,
    0x0C,      0x1F,    GEN_CMD, 2,         0x0D,    0x47,    GEN_CMD, 2,       0x0E,    0x47,
    GEN_CMD,   2,       0x0F,    0x45,      GEN_CMD, 2,       0x10,    0x45,    GEN_CMD, 2,
    0x11,      0x4B,    GEN_CMD, 2,         0x12,    0x4B,    GEN_CMD, 2,       0x13,    0x49,
    GEN_CMD,   2,       0x14,    0x49,      GEN_CMD, 2,       0x15,    0x1F,    GEN_CMD, 2,
    0x16,      0x1E,    GEN_CMD, 2,         0x17,    0x1E,    GEN_CMD, 2,       0x18,    0x40,
    GEN_CMD,   2,       0x19,    0x40,      GEN_CMD, 2,       0x1A,    0x42,    GEN_CMD, 2,
    0x1B,      0x42,    GEN_CMD, 2,         0x1C,    0x1F,    GEN_CMD, 2,       0x1D,    0x1F,
    GEN_CMD,   2,       0x1E,    0x35,      GEN_CMD, 2,       0x1F,    0x1F,    GEN_CMD, 2,
    0x20,      0x15,    GEN_CMD, 2,         0x21,    0x15,    GEN_CMD, 2,       0x22,    0x1F,
    GEN_CMD,   2,       0x23,    0x46,      GEN_CMD, 2,       0x24,    0x46,    GEN_CMD, 2,
    0x25,      0x44,    GEN_CMD, 2,         0x26,    0x44,    GEN_CMD, 2,       0x27,    0x4A,
    GEN_CMD,   2,       0x28,    0x4A,      GEN_CMD, 2,       0x29,    0x48,    GEN_CMD, 2,
    0x2A,      0x48,    GEN_CMD, 2,         0x2B,    0x1F,    GEN_CMD, 2,       0x58,    0x40,
    GEN_CMD,   2,       0x5B,    0x30,      GEN_CMD, 2,       0x5C,    0x0F,    GEN_CMD, 2,
    0x5D,      0x30,    GEN_CMD, 2,         0x5E,    0x01,    GEN_CMD, 2,       0x5F,    0x02,
    GEN_CMD,   2,       0x63,    0x14,      GEN_CMD, 2,       0x64,    0x6A,    GEN_CMD, 2,
    0x67,      0x73,    GEN_CMD, 2,         0x68,    0x11,    GEN_CMD, 2,       0x69,    0x14,
    GEN_CMD,   2,       0x6A,    0x6A,      GEN_CMD, 2,       0x6B,    0x08,    GEN_CMD, 2,
    0x6C,      0x00,    GEN_CMD, 2,         0x6D,    0x00,    GEN_CMD, 2,       0x6E,    0x00,
    GEN_CMD,   2,       0x6F,    0x88,      GEN_CMD, 2,       0x77,    0xDD,    GEN_CMD, 2,
    0x79,      0x0E,    GEN_CMD, 2,         0x7A,    0x0F,    GEN_CMD, 2,       0x7D,    0x14,
    GEN_CMD,   2,       0x7E,    0x82,      GEN_CMD, 2,       0xE0,    0x04,    GEN_CMD, 2,
    0x09,      0x11,    GEN_CMD, 2,         0x0E,    0x48,    GEN_CMD, 2,       0x2B,    0x2B,
    GEN_CMD,   2,       0x2D,    0x03,      GEN_CMD, 2,       0x2E,    0x44,    GEN_CMD, 2,
    0xE0,      0x00,    GEN_CMD, 2,         0xE6,    0x02,    GEN_CMD, 2,       0xE7,    0x0C,
    DCS_CMD,   1,       0x11,    DELAY_CMD, 120,     GEN_CMD, 2,       0xE0,    0x03,    GEN_CMD,
    2,         0x2B,    0x01,    GEN_CMD,   2,       0x2C,    0x00,    GEN_CMD, 2,       0x30,
    0x03,      GEN_CMD, 2,       0x31,      0xBC,    GEN_CMD, 2,       0x32,    0x03,    GEN_CMD,
    2,         0x33,    0xBA,    GEN_CMD,   2,       0x34,    0x03,    GEN_CMD, 2,       0x35,
    0xB4,      GEN_CMD, 2,       0x36,      0x03,    GEN_CMD, 2,       0x37,    0xAE,    GEN_CMD,
    2,         0x38,    0x03,    GEN_CMD,   2,       0x39,    0xA6,    GEN_CMD, 2,       0x3A,
    0x03,      GEN_CMD, 2,       0x3B,      0x97,    GEN_CMD, 2,       0x3C,    0x03,    GEN_CMD,
    2,         0x3D,    0x88,    GEN_CMD,   2,       0x3E,    0x03,    GEN_CMD, 2,       0x3F,
    0x68,      GEN_CMD, 2,       0x40,      0x03,    GEN_CMD, 2,       0x41,    0x4C,    GEN_CMD,
    2,         0x42,    0x03,    GEN_CMD,   2,       0x43,    0x0F,    GEN_CMD, 2,       0x44,
    0x02,      GEN_CMD, 2,       0x45,      0xD2,    GEN_CMD, 2,       0x46,    0x02,    GEN_CMD,
    2,         0x47,    0x58,    GEN_CMD,   2,       0x48,    0x01,    GEN_CMD, 2,       0x49,
    0xE3,      GEN_CMD, 2,       0x4A,      0x01,    GEN_CMD, 2,       0x4B,    0xDF,    GEN_CMD,
    2,         0x4C,    0x01,    GEN_CMD,   2,       0x4D,    0x68,    GEN_CMD, 2,       0x4E,
    0x00,      GEN_CMD, 2,       0x4F,      0xEF,    GEN_CMD, 2,       0x50,    0x00,    GEN_CMD,
    2,         0x51,    0xB1,    GEN_CMD,   2,       0x52,    0x00,    GEN_CMD, 2,       0x53,
    0x70,      GEN_CMD, 2,       0x54,      0x00,    GEN_CMD, 2,       0x55,    0x53,    GEN_CMD,
    2,         0x56,    0x00,    GEN_CMD,   2,       0x57,    0x34,    GEN_CMD, 2,       0x58,
    0x00,      GEN_CMD, 2,       0x59,      0x26,    GEN_CMD, 2,       0x5A,    0x00,    GEN_CMD,
    2,         0x5B,    0x18,    GEN_CMD,   2,       0x5C,    0x00,    GEN_CMD, 2,       0x5D,
    0x11,      GEN_CMD, 2,       0x5E,      0x00,    GEN_CMD, 2,       0x5F,    0x0A,    GEN_CMD,
    2,         0x60,    0x00,    GEN_CMD,   2,       0x61,    0x03,    GEN_CMD, 2,       0x62,
    0x00,      GEN_CMD, 2,       0x63,      0x00,    GEN_CMD, 2,       0x64,    0x03,    GEN_CMD,
    2,         0x65,    0xCC,    GEN_CMD,   2,       0x66,    0x03,    GEN_CMD, 2,       0x67,
    0xCA,      GEN_CMD, 2,       0x68,      0x03,    GEN_CMD, 2,       0x69,    0xC3,    GEN_CMD,
    2,         0x6A,    0x03,    GEN_CMD,   2,       0x6B,    0xBD,    GEN_CMD, 2,       0x6C,
    0x03,      GEN_CMD, 2,       0x6D,      0xB7,    GEN_CMD, 2,       0x6E,    0x03,    GEN_CMD,
    2,         0x6F,    0xAB,    GEN_CMD,   2,       0x70,    0x03,    GEN_CMD, 2,       0x71,
    0x9D,      GEN_CMD, 2,       0x72,      0x03,    GEN_CMD, 2,       0x73,    0x7F,    GEN_CMD,
    2,         0x74,    0x03,    GEN_CMD,   2,       0x75,    0x62,    GEN_CMD, 2,       0x76,
    0x03,      GEN_CMD, 2,       0x77,      0x26,    GEN_CMD, 2,       0x78,    0x02,    GEN_CMD,
    2,         0x79,    0xEA,    GEN_CMD,   2,       0x7A,    0x02,    GEN_CMD, 2,       0x7B,
    0x6F,      GEN_CMD, 2,       0x7C,      0x01,    GEN_CMD, 2,       0x7D,    0xF3,    GEN_CMD,
    2,         0x7E,    0x01,    GEN_CMD,   2,       0x7F,    0xEF,    GEN_CMD, 2,       0x80,
    0x01,      GEN_CMD, 2,       0x81,      0x73,    GEN_CMD, 2,       0x82,    0x00,    GEN_CMD,
    2,         0x83,    0xF6,    GEN_CMD,   2,       0x84,    0x00,    GEN_CMD, 2,       0x85,
    0xB5,      GEN_CMD, 2,       0x86,      0x00,    GEN_CMD, 2,       0x87,    0x77,    GEN_CMD,
    2,         0x88,    0x00,    GEN_CMD,   2,       0x89,    0x5A,    GEN_CMD, 2,       0x8A,
    0x00,      GEN_CMD, 2,       0x8B,      0x3A,    GEN_CMD, 2,       0x8C,    0x00,    GEN_CMD,
    2,         0x8D,    0x2A,    GEN_CMD,   2,       0x8E,    0x00,    GEN_CMD, 2,       0x8F,
    0x1B,      GEN_CMD, 2,       0x90,      0x00,    GEN_CMD, 2,       0x91,    0x13,    GEN_CMD,
    2,         0x92,    0x00,    GEN_CMD,   2,       0x93,    0x0C,    GEN_CMD, 2,       0x94,
    0x00,      GEN_CMD, 2,       0x95,      0x04,    GEN_CMD, 2,       0x96,    0x00,    GEN_CMD,
    2,         0x97,    0x00,    GEN_CMD,   2,       0xE0,    0x00,    DCS_CMD, 1,       0x29,
    DELAY_CMD, 0xFF,
};
constexpr uint8_t lcd_init_sequence_TV080WXM_FT[] = {
    DELAY_CMD, 120, DCS_CMD, 1, 0x11, DELAY_CMD, 120, DCS_CMD, 1, 0x29,
};
}  // namespace

zx_status_t Lcd::GetDisplayId() {
  uint8_t txcmd = READ_DISPLAY_ID_CMD;
  uint8_t rsp[READ_DISPLAY_ID_LEN];
  zx_status_t status = ZX_OK;
  // Create the command using mipi-dsi library
  mipi_dsi_cmd_t cmd;
  status = mipi_dsi::MipiDsi::CreateCommand(&txcmd, 1, rsp, READ_DISPLAY_ID_LEN, COMMAND_GEN, &cmd);
  if (status == ZX_OK) {
    if ((status = dsiimpl_.SendCmd(&cmd, 1)) != ZX_OK) {
      DISP_ERROR("Could not read out Display ID\n");
      return status;
    }
    DISP_INFO("Display ID: 0x%x, 0x%x, 0x%x\n", rsp[0], rsp[1], rsp[2]);
  } else {
    DISP_ERROR("Invalid command (%d)\n", status);
  }

  return status;
}

// This function write DSI commands based on the input buffer.
zx_status_t Lcd::LoadInitTable(const uint8_t* buffer, size_t size) {
  zx_status_t status = ZX_OK;
  size_t i;
  i = 0;
  bool isDCS = false;
  while (i < size) {
    switch (buffer[i]) {
      case DELAY_CMD:
        zx_nanosleep(zx_deadline_after(ZX_MSEC(buffer[i + 1])));
        i += 2;
        break;
      case DCS_CMD:
        isDCS = true;
        __FALLTHROUGH;
      case GEN_CMD:
      default:
        // Create the command using mipi-dsi library
        mipi_dsi_cmd_t cmd;
        status =
            mipi_dsi::MipiDsi::CreateCommand(&buffer[i + 2], buffer[i + 1], NULL, 0, isDCS, &cmd);
        if (status == ZX_OK) {
          if ((status = dsiimpl_.SendCmd(&cmd, 1)) != ZX_OK) {
            DISP_ERROR("Error loading LCD init table. Aborting %d\n", status);
            return status;
          }
        } else {
          DISP_ERROR("Invalid command (%d). Skipping\n", status);
        }
        // increment by payload length
        i += buffer[i + 1] + 2;  // the 2 includes current plus size field
        isDCS = false;
        break;
    }
  }
  return status;
}

zx_status_t Lcd::Disable() {
  ZX_DEBUG_ASSERT(initialized_);
  if (!enabled_) {
    return ZX_OK;
  }
  // First send shutdown command to LCD
  enabled_ = false;
  return LoadInitTable(lcd_shutdown_sequence, sizeof(lcd_shutdown_sequence));
}

zx_status_t Lcd::Enable() {
  ZX_DEBUG_ASSERT(initialized_);
  if (enabled_) {
    return ZX_OK;
  }
  // reset LCD panel via GPIO according to vendor doc
  gpio_config_out(&gpio_, 1);
  gpio_write(&gpio_, 1);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(30)));
  gpio_write(&gpio_, 0);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  gpio_write(&gpio_, 1);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
  // check status
  if (GetDisplayId() != ZX_OK) {
    DISP_ERROR("Cannot communicate with LCD Panel!\n");
    return ZX_ERR_TIMED_OUT;
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // load table
  zx_status_t status;
  if (panel_type_ == PANEL_TV070WSM_FT) {
    status = LoadInitTable(lcd_init_sequence_TV070WSM_FT, sizeof(lcd_init_sequence_TV070WSM_FT));
  } else if (panel_type_ == PANEL_P070ACB_FT) {
    status = LoadInitTable(lcd_init_sequence_P070ACB_FT, sizeof(lcd_init_sequence_P070ACB_FT));
  } else if (panel_type_ == PANEL_TV101WXM_FT) {
    status = LoadInitTable(lcd_init_sequence_TV101WXM_FT, sizeof(lcd_init_sequence_TV101WXM_FT));
  } else if (panel_type_ == PANEL_G101B158_FT) {
    status = LoadInitTable(lcd_init_sequence_G101B158_FT, sizeof(lcd_init_sequence_G101B158_FT));
  } else if (panel_type_ == PANEL_TV080WXM_FT) {
    status = LoadInitTable(lcd_init_sequence_TV080WXM_FT, sizeof(lcd_init_sequence_TV080WXM_FT));
  } else {
    DISP_ERROR("Unsupported panel detected!\n");
    status = ZX_ERR_NOT_SUPPORTED;
  }

  if (status == ZX_OK) {
    // LCD is on now.
    enabled_ = true;
  }
  return status;
}

zx_status_t Lcd::Init(zx_device_t* dsi_dev, zx_device_t* gpio_dev) {
  if (initialized_) {
    return ZX_OK;
  }

  dsiimpl_ = dsi_dev;

  zx_status_t status = device_get_protocol(gpio_dev, ZX_PROTOCOL_GPIO, &gpio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not obtain GPIO protocol\n");
    return status;
  }

  initialized_ = true;

  if (kBootloaderDisplayEnabled) {
    DISP_INFO("LCD Enabled by Bootloader. Disabling before proceeding\n");
    enabled_ = true;
    Disable();
  }

  return ZX_OK;
}

}  // namespace amlogic_display
