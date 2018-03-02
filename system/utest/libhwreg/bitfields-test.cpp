// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fbl/limits.h>
#include <limits.h>
#include <stdio.h>
#include <unittest/unittest.h>

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

// This function exists so that the resulting code can be inspected easily in the
// object file.
void compilation_test() {
    class TestReg32 : public hwreg::RegisterBase<TestReg32, uint32_t> {
    public:
        DEF_FIELD(30, 12, field1);
        DEF_BIT(11, field2);
        DEF_RSVDZ_FIELD(10, 5);
        DEF_FIELD(4, 3, field3);
        DEF_RSVDZ_BIT(2);
        DEF_RSVDZ_BIT(1);
        DEF_FIELD(0, 0, field4);

        static auto Get() { return hwreg::RegisterAddr<TestReg32>(0); }
    };

    volatile uint32_t fake_reg = 1ul << 31;
    hwreg::RegisterIo mmio(&fake_reg);

    auto reg = TestReg32::Get().ReadFrom(&mmio);
    reg.set_field1(0x31234);
    reg.set_field2(1);
    reg.set_field3(2);
    reg.set_field4(0);
    reg.WriteTo(&mmio);
}

template <typename IntType>
static bool struct_sub_bit_test() {
    BEGIN_TEST;

    constexpr unsigned kLastBit = sizeof(IntType) * CHAR_BIT - 1;

    struct StructSubBitTest {
        IntType field;

        DEF_SUBBIT(field, 0, first_bit);
        DEF_SUBBIT(field, 1, mid_bit);
        DEF_SUBBIT(field, kLastBit, last_bit);
    };

    StructSubBitTest val = {};
    EXPECT_EQ(0u, val.first_bit());
    EXPECT_EQ(0u, val.mid_bit());
    EXPECT_EQ(0u, val.last_bit());

    val.set_first_bit(1);
    EXPECT_EQ(1u, val.field);
    EXPECT_EQ(1u, val.first_bit());
    EXPECT_EQ(0u, val.mid_bit());
    EXPECT_EQ(0u, val.last_bit());
    val.set_first_bit(0);

    val.set_mid_bit(1);
    EXPECT_EQ(2u, val.field);
    EXPECT_EQ(0u, val.first_bit());
    EXPECT_EQ(1u, val.mid_bit());
    EXPECT_EQ(0u, val.last_bit());
    val.set_mid_bit(0);

    val.set_last_bit(1);
    EXPECT_EQ(1ull << kLastBit, val.field);
    EXPECT_EQ(0u, val.first_bit());
    EXPECT_EQ(0u, val.mid_bit());
    EXPECT_EQ(1u, val.last_bit());
    val.set_last_bit(0);

    END_TEST;
}

template <typename IntType>
static bool struct_sub_field_test() {
    BEGIN_TEST;

    constexpr unsigned kLastBit = sizeof(IntType) * CHAR_BIT - 1;

    struct StructSubFieldTest {
        IntType field1;
        DEF_SUBFIELD(field1, kLastBit, 0, whole_length);

        IntType field2;
        DEF_SUBFIELD(field2, 2, 2, single_bit);

        IntType field3;
        DEF_SUBFIELD(field3, 2, 1, range1);
        DEF_SUBFIELD(field3, 5, 3, range2);
    };

    StructSubFieldTest val = {};

    // Ensure writing to a whole length field affects all bits
    constexpr IntType kMax = fbl::numeric_limits<IntType>::max();
    EXPECT_EQ(0u, val.whole_length());
    val.set_whole_length(kMax);
    EXPECT_EQ(kMax, val.whole_length());
    EXPECT_EQ(kMax, val.field1);
    val.set_whole_length(0);
    EXPECT_EQ(0, val.whole_length());
    EXPECT_EQ(0, val.field1);

    // Ensure writing to a single bit only affects that bit
    EXPECT_EQ(0u, val.single_bit());
    val.set_single_bit(1);
    EXPECT_EQ(1u, val.single_bit());
    EXPECT_EQ(4u, val.field2);
    val.set_single_bit(0);
    EXPECT_EQ(0u, val.single_bit());
    EXPECT_EQ(0u, val.field2);

    // Ensure writing to adjacent fields does not bleed across
    EXPECT_EQ(0u, val.range1());
    EXPECT_EQ(0u, val.range2());
    val.set_range1(3);
    EXPECT_EQ(3u, val.range1());
    EXPECT_EQ(0u, val.range2());
    EXPECT_EQ(3u << 1, val.field3);
    val.set_range2(1);
    EXPECT_EQ(3u, val.range1());
    EXPECT_EQ(1u, val.range2());
    EXPECT_EQ((3u << 1) | (1u << 3), val.field3);
    val.set_range2(2);
    EXPECT_EQ(3u, val.range1());
    EXPECT_EQ(2u, val.range2());
    EXPECT_EQ((3u << 1) | (2u << 3), val.field3);
    val.set_range1(0);
    EXPECT_EQ(0u, val.range1());
    EXPECT_EQ(2u, val.range2());
    EXPECT_EQ((2u << 3), val.field3);

    END_TEST;
}

static bool reg_rsvdz_test() {
    BEGIN_TEST;

    class TestReg8 : public hwreg::RegisterBase<TestReg8, uint8_t> {
    public:
        DEF_RSVDZ_FIELD(7, 3);

        static auto Get() { return hwreg::RegisterAddr<TestReg8>(0); }
    };
    class TestReg16 : public hwreg::RegisterBase<TestReg16, uint16_t> {
    public:
        DEF_RSVDZ_FIELD(14, 1);

        static auto Get() { return hwreg::RegisterAddr<TestReg16>(0); }
    };
    class TestReg32 : public hwreg::RegisterBase<TestReg32, uint32_t> {
    public:
        DEF_RSVDZ_FIELD(31, 12);
        DEF_RSVDZ_FIELD(10, 5);
        DEF_RSVDZ_BIT(3);

        static auto Get() { return hwreg::RegisterAddr<TestReg32>(0); }
    };
    class TestReg64 : public hwreg::RegisterBase<TestReg64, uint64_t> {
    public:
        DEF_RSVDZ_FIELD(63, 18);
        DEF_RSVDZ_FIELD(10, 0);

        static auto Get() { return hwreg::RegisterAddr<TestReg64>(0); }
    };

    volatile uint64_t fake_reg;
    hwreg::RegisterIo mmio(&fake_reg);

    // Ensure we mask off the RsvdZ bits when we write them back, regardless of
    // what we read them as.
    {
        fake_reg = fbl::numeric_limits<uint8_t>::max();
        auto reg = TestReg8::Get().ReadFrom(&mmio);
        EXPECT_EQ(fbl::numeric_limits<uint8_t>::max(), reg.reg_value());
        reg.WriteTo(&mmio);
        EXPECT_EQ(0x7u, fake_reg);
    }
    {
        fake_reg = fbl::numeric_limits<uint16_t>::max();
        auto reg = TestReg16::Get().ReadFrom(&mmio);
        EXPECT_EQ(fbl::numeric_limits<uint16_t>::max(), reg.reg_value());
        reg.WriteTo(&mmio);
        EXPECT_EQ(0x8001u, fake_reg);
    }
    {
        fake_reg = fbl::numeric_limits<uint32_t>::max();
        auto reg = TestReg32::Get().ReadFrom(&mmio);
        EXPECT_EQ(fbl::numeric_limits<uint32_t>::max(), reg.reg_value());
        reg.WriteTo(&mmio);
        EXPECT_EQ((1ull << 11) | 0x17ull, fake_reg);
    }
    {
        fake_reg = fbl::numeric_limits<uint64_t>::max();
        auto reg = TestReg64::Get().ReadFrom(&mmio);
        EXPECT_EQ(fbl::numeric_limits<uint64_t>::max(), reg.reg_value());
        reg.WriteTo(&mmio);
        EXPECT_EQ(0x7full << 11, fake_reg);
    }

    END_TEST;
}

static bool reg_rsvdz_full_test() {
    BEGIN_TEST;

    class TestReg8 : public hwreg::RegisterBase<TestReg8, uint8_t> {
    public:
        DEF_RSVDZ_FIELD(7, 0);

        static auto Get() { return hwreg::RegisterAddr<TestReg8>(0); }
    };
    class TestReg16 : public hwreg::RegisterBase<TestReg16, uint16_t> {
    public:
        DEF_RSVDZ_FIELD(15, 0);

        static auto Get() { return hwreg::RegisterAddr<TestReg16>(0); }
    };
    class TestReg32 : public hwreg::RegisterBase<TestReg32, uint32_t> {
    public:
        DEF_RSVDZ_FIELD(31, 0);

        static auto Get() { return hwreg::RegisterAddr<TestReg32>(0); }
    };
    class TestReg64 : public hwreg::RegisterBase<TestReg64, uint64_t> {
    public:
        DEF_RSVDZ_FIELD(63, 0);

        static auto Get() { return hwreg::RegisterAddr<TestReg64>(0); }
    };

    volatile uint64_t fake_reg;
    hwreg::RegisterIo mmio(&fake_reg);

    // Ensure we mask off the RsvdZ bits when we write them back, regardless of
    // what we read them as.
    {
        fake_reg = fbl::numeric_limits<uint8_t>::max();
        auto reg = TestReg8::Get().ReadFrom(&mmio);
        EXPECT_EQ(fbl::numeric_limits<uint8_t>::max(), reg.reg_value());
        reg.WriteTo(&mmio);
        EXPECT_EQ(0u, fake_reg);
    }
    {
        fake_reg = fbl::numeric_limits<uint16_t>::max();
        auto reg = TestReg16::Get().ReadFrom(&mmio);
        EXPECT_EQ(fbl::numeric_limits<uint16_t>::max(), reg.reg_value());
        reg.WriteTo(&mmio);
        EXPECT_EQ(0u, fake_reg);
    }
    {
        fake_reg = fbl::numeric_limits<uint32_t>::max();
        auto reg = TestReg32::Get().ReadFrom(&mmio);
        EXPECT_EQ(fbl::numeric_limits<uint32_t>::max(), reg.reg_value());
        reg.WriteTo(&mmio);
        EXPECT_EQ(0u, fake_reg);
    }
    {
        fake_reg = fbl::numeric_limits<uint64_t>::max();
        auto reg = TestReg64::Get().ReadFrom(&mmio);
        EXPECT_EQ(fbl::numeric_limits<uint64_t>::max(), reg.reg_value());
        reg.WriteTo(&mmio);
        EXPECT_EQ(0u, fake_reg);
    }

    END_TEST;
}

static bool reg_field_test() {
    BEGIN_TEST;

    class TestReg8 : public hwreg::RegisterBase<TestReg8, uint8_t> {
    public:
        DEF_FIELD(7, 3, field1);
        DEF_FIELD(2, 0, field2);

        static auto Get() { return hwreg::RegisterAddr<TestReg8>(0); }
    };
    class TestReg16 : public hwreg::RegisterBase<TestReg16, uint16_t> {
    public:
        DEF_FIELD(13, 3, field1);
        DEF_FIELD(2, 1, field2);
        DEF_BIT(0, field3);

        static auto Get() { return hwreg::RegisterAddr<TestReg16>(0); }
    };
    class TestReg32 : public hwreg::RegisterBase<TestReg32, uint32_t> {
    public:
        DEF_FIELD(30, 21, field1);
        DEF_FIELD(20, 12, field2);
        DEF_RSVDZ_FIELD(11, 0);

        static auto Get() { return hwreg::RegisterAddr<TestReg32>(0); }
    };
    class TestReg64 : public hwreg::RegisterBase<TestReg64, uint64_t> {
    public:
        DEF_FIELD(60, 20, field1);
        DEF_FIELD(10, 0, field2);

        static auto Get() { return hwreg::RegisterAddr<TestReg64>(0); }
    };

    volatile uint64_t fake_reg;
    hwreg::RegisterIo mmio(&fake_reg);

    // Ensure modified fields go to the right place, and unspecified bits are
    // preserved.
    {
        constexpr uint8_t kInitVal = 0x42u;
        fake_reg = kInitVal;
        auto reg = TestReg8::Get().ReadFrom(&mmio);
        EXPECT_EQ(kInitVal, reg.reg_value());
        EXPECT_EQ(kInitVal >> 3, reg.field1());
        EXPECT_EQ(0x2u, reg.field2());
        reg.set_field1(0x1fu);
        reg.set_field2(0x1u);
        EXPECT_EQ(0x1fu, reg.field1());
        EXPECT_EQ(0x1u, reg.field2());

        reg.WriteTo(&mmio);
        EXPECT_EQ((0x1fu << 3) | 1, fake_reg);
    }
    {
        constexpr uint16_t kInitVal = 0b1010'1111'0101'0000u;
        fake_reg = kInitVal;
        auto reg = TestReg16::Get().ReadFrom(&mmio);
        EXPECT_EQ(kInitVal, reg.reg_value());
        EXPECT_EQ((kInitVal >> 3) & ((1u << 11) - 1), reg.field1());
        EXPECT_EQ((kInitVal >> 1) & 0x3u, reg.field2());
        EXPECT_EQ(kInitVal & 1u, reg.field3());
        reg.set_field1(42);
        reg.set_field2(2);
        reg.set_field3(1);
        EXPECT_EQ(42u, reg.field1());
        EXPECT_EQ(2u, reg.field2());
        EXPECT_EQ(1u, reg.field3());
        reg.WriteTo(&mmio);
        EXPECT_EQ((0b10u << 14) | (42u << 3) | (2u << 1) | 1u, fake_reg);
    }
    {
        constexpr uint32_t kInitVal = 0xe987'2fffu;
        fake_reg = kInitVal;
        auto reg = TestReg32::Get().ReadFrom(&mmio);
        EXPECT_EQ(kInitVal, reg.reg_value());
        EXPECT_EQ((kInitVal >> 21) & ((1u << 10) - 1), reg.field1());
        EXPECT_EQ((kInitVal >> 12) & ((1u << 9) - 1), reg.field2());
        reg.set_field1(0x3a7);
        reg.set_field2(0x8f);
        EXPECT_EQ(0x3a7u, reg.field1());
        EXPECT_EQ(0x8fu, reg.field2());
        reg.WriteTo(&mmio);
        EXPECT_EQ((0b1u << 31) | (0x3a7u << 21) | (0x8fu << 12), fake_reg);
    }
    {
        constexpr uint64_t kInitVal = 0xfedc'ba98'7654'3210ull;
        fake_reg = kInitVal;
        auto reg = TestReg64::Get().ReadFrom(&mmio);
        EXPECT_EQ(kInitVal, reg.reg_value());
        EXPECT_EQ((kInitVal >> 20) & ((1ull << 41) - 1), reg.field1());
        EXPECT_EQ(kInitVal & ((1ull << 11) - 1), reg.field2());
        reg.set_field1(0x1a2'3456'789aull);
        reg.set_field2(0x78c);
        EXPECT_EQ(0x1a2'3456'789aull, reg.field1());
        EXPECT_EQ(0x78cu, reg.field2());
        reg.WriteTo(&mmio);
        EXPECT_EQ((0b111ull << 61) | (0x1a2'3456'789aull << 20) | (0x86ull << 11) | 0x78cu, fake_reg);
    }

    END_TEST;
}

static bool print_test() {
    BEGIN_TEST;

    class TestReg : public hwreg::RegisterBase<TestReg, uint32_t, hwreg::EnablePrinter> {
    public:
        DEF_RSVDZ_BIT(31);
        DEF_FIELD(30, 21, field1);
        DEF_FIELD(20, 12, field2);
        DEF_RSVDZ_FIELD(11, 0);

        static auto Get() { return hwreg::RegisterAddr<TestReg>(0); }
    };

    volatile uint64_t fake_reg;
    hwreg::RegisterIo mmio(&fake_reg);

    constexpr uint32_t kInitVal = 0xe987'2fffu;
    fake_reg = kInitVal;
    {
        auto reg = TestReg::Get().ReadFrom(&mmio);
        unsigned call_count = 0;
        const char* expected[] = {
            "RsvdZ[31:31]: 0x1 (1)",
            "field1[30:21]: 0x34c (844)",
            "field2[20:12]: 0x072 (114)",
            "RsvdZ[11:0]: 0xfff (4095)",
        };
        reg.Print([&](const char* buf) {
            EXPECT_STR_EQ(expected[call_count], buf, "mismatch");
            call_count++;
        });
        EXPECT_EQ(fbl::count_of(expected), call_count);
    }

    class TestReg2 : public hwreg::RegisterBase<TestReg2, uint32_t, hwreg::EnablePrinter> {
    public:
        DEF_FIELD(30, 21, field1);
        DEF_FIELD(20, 12, field2);

        static auto Get() { return hwreg::RegisterAddr<TestReg2>(0); }
    };

    {
        auto reg = TestReg2::Get().ReadFrom(&mmio);
        unsigned call_count = 0;
        const char* expected[] = {
            "field1[30:21]: 0x34c (844)",
            "field2[20:12]: 0x072 (114)",
            "unknown set bits: 0x80000fff",
        };
        reg.Print([&](const char* buf) {
            EXPECT_STR_EQ(expected[call_count], buf, "mismatch");
            call_count++;
        });
        EXPECT_EQ(fbl::count_of(expected), call_count);
    }

    END_TEST;
}

// Test using the "fluent" style of chaining calls, like:
// TestReg::Get().ReadFrom(&mmio).set_field1(0x234).set_field2(0x123).WriteTo(&mmio);
static bool set_chaining_test() {
    BEGIN_TEST;

    class TestReg : public hwreg::RegisterBase<TestReg, uint32_t> {
    public:
        DEF_RSVDZ_BIT(31);
        DEF_FIELD(30, 21, field1);
        DEF_FIELD(20, 12, field2);
        DEF_RSVDZ_FIELD(11, 0);

        static auto Get() { return hwreg::RegisterAddr<TestReg>(0); }
    };

    volatile uint32_t fake_reg;
    hwreg::RegisterIo mmio(&fake_reg);

    // With ReadFrom from a RegAddr
    fake_reg = ~0u;
    TestReg::Get().ReadFrom(&mmio).set_field1(0x234).set_field2(0x123).WriteTo(&mmio);
    EXPECT_EQ((0x234u << 21) | (0x123u << 12), fake_reg);

    // With ReadFrom from TestReg
    fake_reg = ~0u;
    auto reg = TestReg::Get().FromValue(0);
    reg.ReadFrom(&mmio).set_field1(0x234).set_field2(0x123).WriteTo(&mmio);
    EXPECT_EQ((0x234u << 21) | (0x123u << 12), fake_reg);

    END_TEST;
}

// Compile-time test that not enabling printing functions provides a size reduction
static void printer_size_reduction() {
    class TestRegWithPrinter : public hwreg::RegisterBase<TestRegWithPrinter, uint64_t,
                                      hwreg::EnablePrinter> {
    };
    class TestRegWithoutPrinter : public hwreg::RegisterBase<TestRegWithoutPrinter, uint64_t> {
    };

    static_assert(sizeof(TestRegWithPrinter) > sizeof(TestRegWithoutPrinter), "");
}

#define RUN_TEST_FOR_UINTS(test) \
        RUN_TEST(test<uint8_t>) \
        RUN_TEST(test<uint16_t>) \
        RUN_TEST(test<uint32_t>) \
        RUN_TEST(test<uint64_t>)

BEGIN_TEST_CASE(libhwreg_tests)
RUN_TEST_FOR_UINTS(struct_sub_bit_test)
RUN_TEST_FOR_UINTS(struct_sub_field_test)
RUN_TEST(reg_rsvdz_test)
RUN_TEST(reg_rsvdz_full_test)
RUN_TEST(reg_field_test)
RUN_TEST(print_test)
RUN_TEST(set_chaining_test)
END_TEST_CASE(libhwreg_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
