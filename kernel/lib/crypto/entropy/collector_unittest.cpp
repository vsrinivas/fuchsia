// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/entropy/collector.h>
#include <unittest.h>

namespace crypto {

namespace entropy {

namespace {

class MockCollector : public Collector {
public:
    MockCollector(size_t entropy_per_1000_bytes)
         : Collector("mock", entropy_per_1000_bytes) {}

    ~MockCollector() {}

    size_t DrawEntropy(uint8_t* buf, size_t len) override { return 0; }
};

bool entropy_estimate_test(void*) {
    BEGIN_TEST;

    MockCollector ec_1(1);
    EXPECT_EQ(0u, ec_1.BytesNeeded(0),
              "bad entropy estimate (entropy per 1000 bytes = 1, bits = 0");
    EXPECT_EQ(1000u, ec_1.BytesNeeded(1),
              "bad entropy estimate (entropy per 1000 bytes = 1, bits = 1");
    EXPECT_EQ(2000u, ec_1.BytesNeeded(2),
              "bad entropy estimate (entropy per 1000 bytes = 1, bits = 2");
    EXPECT_EQ(1048575000u, ec_1.BytesNeeded((1024*1024-1)),
              "bad entropy estimate (entropy per 1000 bytes = 1, bits = (1024*1024-1)");
    EXPECT_EQ(1048576000u, ec_1.BytesNeeded((1024*1024)),
              "bad entropy estimate (entropy per 1000 bytes = 1, bits = (1024*1024)");

    MockCollector ec_2(2);
    EXPECT_EQ(0u, ec_2.BytesNeeded(0),
              "bad entropy estimate (entropy per 1000 bytes = 2, bits = 0");
    EXPECT_EQ(500u, ec_2.BytesNeeded(1),
              "bad entropy estimate (entropy per 1000 bytes = 2, bits = 1");
    EXPECT_EQ(1000u, ec_2.BytesNeeded(2),
              "bad entropy estimate (entropy per 1000 bytes = 2, bits = 2");
    EXPECT_EQ(1500u, ec_2.BytesNeeded(3),
              "bad entropy estimate (entropy per 1000 bytes = 2, bits = 3");
    EXPECT_EQ(524287500u, ec_2.BytesNeeded((1024*1024-1)),
              "bad entropy estimate (entropy per 1000 bytes = 2, bits = (1024*1024-1)");
    EXPECT_EQ(524288000u, ec_2.BytesNeeded((1024*1024)),
              "bad entropy estimate (entropy per 1000 bytes = 2, bits = (1024*1024)");

    MockCollector ec_3999(3999);
    EXPECT_EQ(0u, ec_3999.BytesNeeded(0),
              "bad entropy estimate (entropy per 1000 bytes = 3999, bits = 0");
    EXPECT_EQ(1u, ec_3999.BytesNeeded(1),
              "bad entropy estimate (entropy per 1000 bytes = 3999, bits = 1");
    EXPECT_EQ(1000u, ec_3999.BytesNeeded(3998),
              "bad entropy estimate (entropy per 1000 bytes = 3999, bits = 3998");
    EXPECT_EQ(1000u, ec_3999.BytesNeeded(3999),
              "bad entropy estimate (entropy per 1000 bytes = 3999, bits = 3999");
    EXPECT_EQ(1001u, ec_3999.BytesNeeded(4000),
              "bad entropy estimate (entropy per 1000 bytes = 3999, bits = 4000");
    EXPECT_EQ(262210u, ec_3999.BytesNeeded((1024*1024-1)),
              "bad entropy estimate (entropy per 1000 bytes = 3999, bits = (1024*1024-1)");
    EXPECT_EQ(262210u, ec_3999.BytesNeeded((1024*1024)),
              "bad entropy estimate (entropy per 1000 bytes = 3999, bits = (1024*1024)");

    MockCollector ec_4000(4000);
    EXPECT_EQ(0u, ec_4000.BytesNeeded(0),
              "bad entropy estimate (entropy per 1000 bytes = 4000, bits = 0");
    EXPECT_EQ(1u, ec_4000.BytesNeeded(1),
              "bad entropy estimate (entropy per 1000 bytes = 4000, bits = 1");
    EXPECT_EQ(1000u, ec_4000.BytesNeeded(3999),
              "bad entropy estimate (entropy per 1000 bytes = 4000, bits = 3999");
    EXPECT_EQ(1000u, ec_4000.BytesNeeded(4000),
              "bad entropy estimate (entropy per 1000 bytes = 4000, bits = 4000");
    EXPECT_EQ(1001u, ec_4000.BytesNeeded(4001),
              "bad entropy estimate (entropy per 1000 bytes = 4000, bits = 4001");
    EXPECT_EQ(262144u, ec_4000.BytesNeeded((1024*1024-1)),
              "bad entropy estimate (entropy per 1000 bytes = 4000, bits = (1024*1024-1)");
    EXPECT_EQ(262144u, ec_4000.BytesNeeded((1024*1024)),
              "bad entropy estimate (entropy per 1000 bytes = 4000, bits = (1024*1024)");

    MockCollector ec_4001(4001);
    EXPECT_EQ(0u, ec_4001.BytesNeeded(0),
              "bad entropy estimate (entropy per 1000 bytes = 4001, bits = 0");
    EXPECT_EQ(1u, ec_4001.BytesNeeded(1),
              "bad entropy estimate (entropy per 1000 bytes = 4001, bits = 1");
    EXPECT_EQ(1000u, ec_4001.BytesNeeded(4000),
              "bad entropy estimate (entropy per 1000 bytes = 4001, bits = 4000");
    EXPECT_EQ(1000u, ec_4001.BytesNeeded(4001),
              "bad entropy estimate (entropy per 1000 bytes = 4001, bits = 4001");
    EXPECT_EQ(1001u, ec_4001.BytesNeeded(4002),
              "bad entropy estimate (entropy per 1000 bytes = 4001, bits = 4002");
    EXPECT_EQ(262079u, ec_4001.BytesNeeded((1024*1024-1)),
              "bad entropy estimate (entropy per 1000 bytes = 4001, bits = (1024*1024-1)");
    EXPECT_EQ(262079u, ec_4001.BytesNeeded((1024*1024)),
              "bad entropy estimate (entropy per 1000 bytes = 4001, bits = (1024*1024)");

    MockCollector ec_7999(7999);
    EXPECT_EQ(0u, ec_7999.BytesNeeded(0),
              "bad entropy estimate (entropy per 1000 bytes = 7999, bits = 0");
    EXPECT_EQ(1u, ec_7999.BytesNeeded(1),
              "bad entropy estimate (entropy per 1000 bytes = 7999, bits = 1");
    EXPECT_EQ(1000u, ec_7999.BytesNeeded(7998),
              "bad entropy estimate (entropy per 1000 bytes = 7999, bits = 7998");
    EXPECT_EQ(1000u, ec_7999.BytesNeeded(7999),
              "bad entropy estimate (entropy per 1000 bytes = 7999, bits = 7999");
    EXPECT_EQ(1001u, ec_7999.BytesNeeded(8000),
              "bad entropy estimate (entropy per 1000 bytes = 7999, bits = 8000");
    EXPECT_EQ(131089u, ec_7999.BytesNeeded((1024*1024-1)),
              "bad entropy estimate (entropy per 1000 bytes = 7999, bits = (1024*1024-1)");
    EXPECT_EQ(131089u, ec_7999.BytesNeeded((1024*1024)),
              "bad entropy estimate (entropy per 1000 bytes = 7999, bits = (1024*1024)");

    MockCollector ec_8000(8000);
    EXPECT_EQ(0u, ec_8000.BytesNeeded(0),
              "bad entropy estimate (entropy per 1000 bytes = 8000, bits = 0");
    EXPECT_EQ(1u, ec_8000.BytesNeeded(1),
              "bad entropy estimate (entropy per 1000 bytes = 8000, bits = 1");
    EXPECT_EQ(1000u, ec_8000.BytesNeeded(7999),
              "bad entropy estimate (entropy per 1000 bytes = 8000, bits = 7999");
    EXPECT_EQ(1000u, ec_8000.BytesNeeded(8000),
              "bad entropy estimate (entropy per 1000 bytes = 8000, bits = 8000");
    EXPECT_EQ(1001u, ec_8000.BytesNeeded(8001),
              "bad entropy estimate (entropy per 1000 bytes = 8000, bits = 8001");
    EXPECT_EQ(131072u, ec_8000.BytesNeeded((1024*1024-1)),
              "bad entropy estimate (entropy per 1000 bytes = 8000, bits = (1024*1024-1)");
    EXPECT_EQ(131072u, ec_8000.BytesNeeded((1024*1024)),
              "bad entropy estimate (entropy per 1000 bytes = 8000, bits = (1024*1024)");

    END_TEST;
}

} // namespace

UNITTEST_START_TESTCASE(entropy_collector_tests)
UNITTEST("test entropy estimates", entropy_estimate_test)
UNITTEST_END_TESTCASE(entropy_collector_tests, "entropy_collector",
                      "Test entropy collector implementation.",
                      nullptr, nullptr);

} // namespace entropy

} // namespace crypto
