#include "arm_gicv2.h"

#include <lib/unittest/unittest.h>

bool test_gic_translator_translate() {
  BEGIN_TEST;

  arm_gicv2::CpuMaskTranslator translator;

  translator.SetGicIdForLogicalId(0, 7);
  ASSERT_EQ(0b10000000u, translator.GetGicMask(0));

  translator.SetGicIdForLogicalId(1, 2);
  ASSERT_EQ(0b00000100u, translator.GetGicMask(1));

  translator.SetGicIdForLogicalId(2, 0);
  ASSERT_EQ(0b00000001u, translator.GetGicMask(2));

  END_TEST;
}

bool test_gic_translator_translate_mask() {
  BEGIN_TEST;

  arm_gicv2::CpuMaskTranslator translator;

  translator.SetGicIdForLogicalId(0, 7);
  translator.SetGicIdForLogicalId(1, 2);
  translator.SetGicIdForLogicalId(2, 0);

  ASSERT_EQ(0b10000101u, translator.LogicalMaskToGic(0b00000111));
  ASSERT_EQ(0b10000000u, translator.LogicalMaskToGic(0b00000001));

  END_TEST;
}

bool test_determine_local_mask() {
  BEGIN_TEST;

  {  // From second reg in first target.
    uint32_t targets[8]{
        0x00800000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    };

    ASSERT_EQ(0b10000000u, gic_determine_local_mask([&](int target) {
                DEBUG_ASSERT(target < 8);
                return targets[target];
              }),
              "");
  }

  {  // From last reg in last target.
    uint32_t targets[8]{
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000080,
    };

    ASSERT_EQ(0b10000000u, gic_determine_local_mask([&](int target) {
                DEBUG_ASSERT(target < 8);
                return targets[target];
              }),
              "");
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(gicv2_tests)
UNITTEST("Set and lookup translations.", test_gic_translator_translate)
UNITTEST("Translate full cpu masks.", test_gic_translator_translate_mask)
UNITTEST("Determine local mask from target registers.", test_determine_local_mask)
UNITTEST_END_TESTCASE(gicv2_tests, "gicv2", "Tests relating to ARM GICv2 handling.")
