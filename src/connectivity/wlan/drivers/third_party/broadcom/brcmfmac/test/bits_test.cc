/*
 * Copyright (c) 2022 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bits.h"

#include <cstdint>

#include <gmock/gmock.h>

#include "gtest/gtest.h"

namespace {

#define BRCMF_MOCK_STATUS_LIST \
  X(ONE)                       \
  X(TWO)

#define X(MOCK_STATUS) MOCK_STATUS,
enum class brcmf_mock_status_bit_t : uint8_t { BRCMF_MOCK_STATUS_LIST };
#undef X

TEST(Bits, GetBit) {
  EXPECT_EQ(1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE),
            brcmf_bit(brcmf_mock_status_bit_t::ONE));
  EXPECT_EQ(1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO),
            brcmf_bit(brcmf_mock_status_bit_t::TWO));
}

TEST(Bits, TestBit) {
  unsigned long status = 1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE);
  EXPECT_TRUE(brcmf_test_bit(brcmf_mock_status_bit_t::ONE, status));
  EXPECT_FALSE(brcmf_test_bit(brcmf_mock_status_bit_t::TWO, status));
}

TEST(Bits, TestBitAtomic) {
  std::atomic<unsigned long> status = 1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE);
  EXPECT_TRUE(brcmf_test_bit(brcmf_mock_status_bit_t::ONE, &status));
  EXPECT_FALSE(brcmf_test_bit(brcmf_mock_status_bit_t::TWO, &status));
}

TEST(Bits, TestAndSetBitFalse) {
  std::atomic<unsigned long> status = 1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO);
  EXPECT_FALSE(brcmf_test_and_set_bit(brcmf_mock_status_bit_t::ONE, &status));
  EXPECT_EQ(1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE) |
                1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO),
            status);
}

TEST(Bits, TestAndSetBitTrue) {
  std::atomic<unsigned long> status = 1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO) |
                                      1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE);
  EXPECT_TRUE(brcmf_test_and_set_bit(brcmf_mock_status_bit_t::ONE, &status));
  EXPECT_EQ(1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE) |
                1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO),
            status);
}

TEST(Bits, TestAndClearBitTrue) {
  std::atomic<unsigned long> status = 1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE) |
                                      1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO);
  EXPECT_TRUE(brcmf_test_and_clear_bit(brcmf_mock_status_bit_t::ONE, &status));
  EXPECT_EQ(1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO), status);
}

TEST(Bits, TestAndClearBitFalse) {
  std::atomic<unsigned long> status = 1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO);
  EXPECT_FALSE(brcmf_test_and_clear_bit(brcmf_mock_status_bit_t::ONE, &status));
  EXPECT_EQ(1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO), status);
}

TEST(Bits, ClearBit) {
  std::atomic<unsigned long> status = 1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE) |
                                      1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO);
  brcmf_clear_bit(brcmf_mock_status_bit_t::ONE, &status);
  EXPECT_EQ(1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO), status);
}

TEST(Bits, ClearBitNoOp) {
  std::atomic<unsigned long> status = 1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO);
  brcmf_clear_bit(brcmf_mock_status_bit_t::ONE, &status);
  EXPECT_EQ(1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO), status);
}

TEST(Bits, SetBit) {
  std::atomic<unsigned long> status = 1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO);
  brcmf_set_bit(brcmf_mock_status_bit_t::ONE, &status);
  EXPECT_EQ(1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE) |
                1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO),
            status);
}

TEST(Bits, SetBitNoOp) {
  std::atomic<unsigned long> status = 1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE) |
                                      1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO);
  brcmf_set_bit(brcmf_mock_status_bit_t::ONE, &status);
  EXPECT_EQ(1U << static_cast<size_t>(brcmf_mock_status_bit_t::ONE) |
                1U << static_cast<size_t>(brcmf_mock_status_bit_t::TWO),
            status);
}

}  // namespace
