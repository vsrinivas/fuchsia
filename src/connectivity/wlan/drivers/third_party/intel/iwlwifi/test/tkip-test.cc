// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.
//
// Unittest code for the TKIP library.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/tkip.h"

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/compiler.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"

using namespace std;

namespace {

struct TestVector {
  uint8_t key[16];
  uint8_t addr[6];
  uint32_t iv32;
  uint16_t iv16;
  // below are the expected results.
  uint16_t p1k[TKIP_P1K_SIZE];
  uint8_t rc4key[RC4_KEY_SIZE];
};

class TkipTest : public zxtest::Test {
 public:
  TkipTest() {}
  ~TkipTest() {}

  bool cmp_uint16s(const char* msg, const uint16_t* expected, const uint16_t* actual, size_t size) {
    if (!memcmp(expected, actual, size)) {
      return true;
    } else {
      printf("%s\n", msg);
      for (size_t i = 0; i < size; i++) {
        printf("  expected[%zu]: 0x%04x  actual[%zu]: 0x%04x\n", i, expected[i], i, actual[i]);
      }
      return false;
    }
  }
};

TEST_F(TkipTest, TestVectorFromStd) {
  // From IEEE AMENDMENT 6: MEDIUM ACCESS CONTROL (MAC) SECURITY ENHANCEMENTS Std 802.11i-2004
  struct TestVector vector[] = {
      {
          // Test vector #1:
          .key =
              {
                  0x00,
                  0x01,
                  0x02,
                  0x03,
                  0x04,
                  0x05,
                  0x06,
                  0x07,
                  0x08,
                  0x09,
                  0x0A,
                  0x0B,
                  0x0C,
                  0x0D,
                  0x0E,
                  0x0F,
              },  // [LSB on left, MSB on right]
          .addr =
              {
                  0x10,
                  0x22,
                  0x33,
                  0x44,
                  0x55,
                  0x66,
              },
          .iv32 = 0x00000000,
          .iv16 = 0x0000,
          .p1k =
              {
                  0x3DD2,
                  0x016E,
                  0x76F4,
                  0x8697,
                  0xB2E8,
              },
          .rc4key =
              {
                  0x00,
                  0x20,
                  0x00,
                  0x33,
                  0xEA,
                  0x8D,
                  0x2F,
                  0x60,
                  0xCA,
                  0x6D,
                  0x13,
                  0x74,
                  0x23,
                  0x4A,
                  0x66,
                  0x0B,
              },
      },

      {
          // Test vector #2:
          .key =
              {
                  0x00,
                  0x01,
                  0x02,
                  0x03,
                  0x04,
                  0x05,
                  0x06,
                  0x07,
                  0x08,
                  0x09,
                  0x0A,
                  0x0B,
                  0x0C,
                  0x0D,
                  0x0E,
                  0x0F,
              },  // [LSB on left, MSB on right]
          .addr =
              {
                  0x10,
                  0x22,
                  0x33,
                  0x44,
                  0x55,
                  0x66,
              },
          .iv32 = 0x00000000,
          .iv16 = 0x0001,
          .p1k =
              {
                  0x3DD2,
                  0x016E,
                  0x76F4,
                  0x8697,
                  0xB2E8,
              },
          .rc4key =
              {
                  0x00,
                  0x20,
                  0x01,
                  0x90,
                  0xFF,
                  0xDC,
                  0x31,
                  0x43,
                  0x89,
                  0xA9,
                  0xD9,
                  0xD0,
                  0x74,
                  0xFD,
                  0x20,
                  0xAA,
              },
      },

      {
          // Test vector #3:
          .key =
              {
                  0x63,
                  0x89,
                  0x3B,
                  0x25,
                  0x08,
                  0x40,
                  0xB8,
                  0xAE,
                  0x0B,
                  0xD0,
                  0xFA,
                  0x7E,
                  0x61,
                  0xD2,
                  0x78,
                  0x3E,
              },  // [LSB on left, MSB on right]
          .addr =
              {
                  0x64,
                  0xF2,
                  0xEA,
                  0xED,
                  0xDC,
                  0x25,
              },
          .iv32 = 0x20DCFD43,
          .iv16 = 0xFFFF,
          .p1k =
              {
                  0x7C67,
                  0x49D7,
                  0x9724,
                  0xB5E9,
                  0xB4F1,
              },
          .rc4key =
              {
                  0xFF,
                  0x7F,
                  0xFF,
                  0x93,
                  0x81,
                  0x0F,
                  0xC6,
                  0xE5,
                  0x8F,
                  0x5D,
                  0xD3,
                  0x26,
                  0x25,
                  0x15,
                  0x44,
                  0xCE,
              },
      },

      {
          // Test vector #4:
          .key =
              {
                  0x63,
                  0x89,
                  0x3B,
                  0x25,
                  0x08,
                  0x40,
                  0xB8,
                  0xAE,
                  0x0B,
                  0xD0,
                  0xFA,
                  0x7E,
                  0x61,
                  0xD2,
                  0x78,
                  0x3E,
              },  // [LSB on left, MSB on right]
          .addr =
              {
                  0x64,
                  0xF2,
                  0xEA,
                  0xED,
                  0xDC,
                  0x25,
              },
          .iv32 = 0x20DCFD44,
          .iv16 = 0x0000,
          .p1k =
              {
                  0x5A5D,
                  0x73A8,
                  0xA859,
                  0x2EC1,
                  0xDC8B,
              },
          .rc4key =
              {
                  0x00,
                  0x20,
                  0x00,
                  0x49,
                  0x8C,
                  0xA4,
                  0x71,
                  0xFC,
                  0xFB,
                  0xFA,
                  0xA1,
                  0x6E,
                  0x36,
                  0x10,
                  0xF0,
                  0x05,
              },
      },

      {
          // Test vector #5:
          .key =
              {
                  0x98,
                  0x3A,
                  0x16,
                  0xEF,
                  0x4F,
                  0xAC,
                  0xB3,
                  0x51,
                  0xAA,
                  0x9E,
                  0xCC,
                  0x27,
                  0x1D,
                  0x73,
                  0x09,
                  0xE2,
              },  // [LSB on left, MSB on right]
          .addr =
              {
                  0x50,
                  0x9C,
                  0x4B,
                  0x17,
                  0x27,
                  0xD9,
              },
          .iv32 = 0xF0A410FC,
          .iv16 = 0x058C,
          .p1k =
              {
                  0xF2DF,
                  0xEBB1,
                  0x88D3,
                  0x5923,
                  0xA07C,
              },
          .rc4key =
              {
                  0x05,
                  0x25,
                  0x8C,
                  0xF4,
                  0xD8,
                  0x51,
                  0x52,
                  0xF4,
                  0xD9,
                  0xAF,
                  0x1A,
                  0x64,
                  0xF1,
                  0xD0,
                  0x70,
                  0x21,
              },
      },

      {
          // Test vector #6:
          .key =
              {
                  0x98,
                  0x3A,
                  0x16,
                  0xEF,
                  0x4F,
                  0xAC,
                  0xB3,
                  0x51,
                  0xAA,
                  0x9E,
                  0xCC,
                  0x27,
                  0x1D,
                  0x73,
                  0x09,
                  0xE2,
              },  // [LSB on left, MSB on right]
          .addr =
              {
                  0x50,
                  0x9C,
                  0x4B,
                  0x17,
                  0x27,
                  0xD9,
              },
          .iv32 = 0xF0A410FC,
          .iv16 = 0x058D,
          .p1k =
              {
                  0xF2DF,
                  0xEBB1,
                  0x88D3,
                  0x5923,
                  0xA07C,
              },
          .rc4key =
              {
                  0x05,
                  0x25,
                  0x8D,
                  0x09,
                  0xF8,
                  0x15,
                  0x43,
                  0xB7,
                  0x6A,
                  0x59,
                  0x6F,
                  0xC2,
                  0xC6,
                  0x73,
                  0x8B,
                  0x30,
              },
      },

      {
          // Test vector #7:
          .key =
              {
                  0xC8,
                  0xAD,
                  0xC1,
                  0x6A,
                  0x8B,
                  0x4D,
                  0xDA,
                  0x3B,
                  0x4D,
                  0xD5,
                  0xB6,
                  0x54,
                  0x38,
                  0x35,
                  0x9B,
                  0x05,
              },  // [LSB on left, MSB on right]
          .addr =
              {
                  0x94,
                  0x5E,
                  0x24,
                  0x4E,
                  0x4D,
                  0x6E,
              },
          .iv32 = 0x8B1573B7,
          .iv16 = 0x30F8,
          .p1k =
              {
                  0xEFF1,
                  0x3F38,
                  0xA364,
                  0x60A9,
                  0x76F3,
              },
          .rc4key =
              {
                  0x30,
                  0x30,
                  0xF8,
                  0x65,
                  0x0D,
                  0xA0,
                  0x73,
                  0xEA,
                  0x61,
                  0x4E,
                  0xA8,
                  0xF4,
                  0x74,
                  0xEE,
                  0x03,
                  0x19,
              },
      },

      {
          // Test vector #8:
          .key =
              {
                  0xC8,
                  0xAD,
                  0xC1,
                  0x6A,
                  0x8B,
                  0x4D,
                  0xDA,
                  0x3B,
                  0x4D,
                  0xD5,
                  0xB6,
                  0x54,
                  0x38,
                  0x35,
                  0x9B,
                  0x05,
              },  // [LSB on left, MSB on right]
          .addr =
              {
                  0x94,
                  0x5E,
                  0x24,
                  0x4E,
                  0x4D,
                  0x6E,
              },
          .iv32 = 0x8B1573B7,
          .iv16 = 0x30F9,
          .p1k =
              {
                  0xEFF1,
                  0x3F38,
                  0xA364,
                  0x60A9,
                  0x76F3,
              },
          .rc4key =
              {
                  0x30,
                  0x30,
                  0xF9,
                  0x31,
                  0x55,
                  0xCE,
                  0x29,
                  0x34,
                  0x37,
                  0xCC,
                  0x76,
                  0x71,
                  0x27,
                  0x16,
                  0xAB,
                  0x8F,
              },
      },
  };

  for (size_t i = 0; i < ARRAY_SIZE(vector); i++) {
    char msg[80];
    snprintf(msg, sizeof(msg), "P1K[%zu] is not equal: ", i);

    static constexpr size_t key_len = sizeof(vector[i].key);
    auto key_conf =
        reinterpret_cast<ieee80211_key_conf*>(malloc(sizeof(ieee80211_key_conf) + key_len));
    key_conf->rx_seq = ((uint64_t)vector[i].iv32) << 16;
    memcpy(key_conf->key, vector[i].key, key_len);

    uint16_t p1k[TKIP_P1K_SIZE];
    ieee80211_get_tkip_rx_p1k(key_conf, vector[i].addr, p1k);

    EXPECT_TRUE(cmp_uint16s(msg, vector[i].p1k, p1k, TKIP_P1K_SIZE));

    free(key_conf);
  }
}

}  // namespace
