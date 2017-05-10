#include <err.h>
#include <lib/memory_limit.h>
#include <sys/types.h>
#include <unittest.h>

typedef struct {
    uintptr_t base;
    size_t size;
} test_range_t;

// Memory map read off a NUC
static const test_range_t nuc_ranges[] = {
    {0, 0x58000},
    {0x59000, 0x45000},
    {0x100000, 0x85d8b000},
    {0x85eb6000, 0x4375000},
    {0x8b2ff000, 0x1000},
    {0x100000000, 0x36f000000},
};

static const mem_limit_ctx_t nuc_ctx = {
    .kernel_base = 0x100000,
    .kernel_size = 4 * MB,
    .ramdisk_base = 0x818e4000,
    .ramdisk_size = 4 * MB,
    .memory_limit = 0,
    .found_kernel = 0,
    .found_ramdisk = 0,
};

// Memory map read off an Acer12 Switch
static const test_range_t switch_alpha_12_ranges[] = {
    {0, 0x58000},
    {0x59000, 0x2d000},
    {0x100000, 0x7359d000},
    {0x736c8000, 0xb1000},
    {0x74079000, 0x16215000},
    {0x8aefe000, 0x1000},
    {0x100000000, 0x6f000000},
};

static const mem_limit_ctx_t switch_alpha_12_ctx = {
    .kernel_base = 0x100000,
    .kernel_size = 4u * MB,
    .ramdisk_base = 0x71b20000,
    .ramdisk_size = 4u * MB,
    .memory_limit = 0,
    .found_kernel = 0,
    .found_ramdisk = 0,
};

// rpi3 has a single contiguous 512MB block of memory
static const test_range_t rpi3_ranges[] = {
    {0xffff000000000000, 0x20000000},
};

static const mem_limit_ctx_t rpi3_ctx = {
    .kernel_base = 0xffff000000000000,
    .kernel_size = 4 * MB,
    .ramdisk_base = 0xffff000007d44000,
    .ramdisk_size = 3u * MB,
    .memory_limit = 0,
    .found_kernel = 0,
    .found_ramdisk = 0,
};

typedef struct {
    mem_limit_ctx_t ctx;
    const test_range_t* ranges;
    size_t range_cnt;
} platform_test_case_t;

// Run a test against a given platform's configuration using a provided memory
// limit. Tests are generated via macros below.
static bool test_runner(const platform_test_case_t test, size_t mem_limit) {
    BEGIN_TEST;
    auto ctx = test.ctx;
    size_t size = 0;
    iovec_t vecs[2];
    size_t total_platform_size = 0;


    ctx.memory_limit = mem_limit;
    ctx.found_kernel = 0;
    ctx.found_ramdisk = 0;

    for (size_t i = 0; i < test.range_cnt; i++) {
        size_t used;
        total_platform_size += test.ranges[i].size;
        status_t status = mem_limit_get_iovs(&ctx, test.ranges[i].base, test.ranges[i].size, vecs, &used);

        REQUIRE_EQ(NO_ERROR, status, "checking mem_limit_get_iovs status");

        for (size_t j = 0; j < used; j++) {
            size += vecs[j].iov_len;
        }
    }

    EXPECT_TRUE(ctx.found_kernel, "checking the kernel was found");
    EXPECT_TRUE(ctx.found_ramdisk, "checking the ramdisk was found");
    if (mem_limit > total_platform_size) {
        EXPECT_LE(size, total_platform_size, "limit > platform size, so size should equal the platform size");
    } else {
        EXPECT_EQ(mem_limit, size, "check that size equals the limit");
    }
    EXPECT_LE(size, total_platform_size, "check the size is smaller than the total range");

    END_TEST;
}

// TODO: These tests ensure that we segment things up and find the kernel and
// ramdisk, but they don't really cover any interesting cases.  They will be
// more useful if some specific cases are written, but those should be easily
// addable as we find new problems down the line.
const platform_test_case_t test_cases[] = {
    {nuc_ctx, nuc_ranges, countof(nuc_ranges)},
    {switch_alpha_12_ctx, switch_alpha_12_ranges, countof(switch_alpha_12_ranges)},
    {rpi3_ctx, rpi3_ranges, countof(rpi3_ranges)},
};

// Test that the memory limit is expanded if the ramdisk would otherwise be
// truncated for being too large.
static bool ml_test_large_ramdisk(void* context) {
    BEGIN_TEST;
    mem_limit_ctx_t ctx = nuc_ctx;
    size_t memory_limit = 64 * MB;
    size_t size = 0;
    size_t used;
    iovec_t vecs[2];

    ctx.ramdisk_size = 64 * MB;
    ctx.memory_limit = memory_limit;
    for (size_t i = 0; i < countof(nuc_ranges); i++) {
        status_t status = mem_limit_get_iovs(&ctx, nuc_ranges[i].base, nuc_ranges[i].size, vecs, &used);
        EXPECT_EQ(NO_ERROR, status, "checking status");
        for (size_t i = 0; i < used; i++) {
            size += vecs[i].iov_len;
        }
    }

    EXPECT_EQ(true, ctx.found_kernel, "checking kernel");
    EXPECT_EQ(true, ctx.found_ramdisk, "checking ramdisk");
    EXPECT_NEQ(memory_limit, size, "checking that size and limit don't match");
    EXPECT_EQ(ctx.kernel_size + ctx.ramdisk_size, size, "checking the limit grew to fit kernel + ramdisk");
    END_TEST;
}

// Generate tests against the test runner so that we can test all tests pass or fail against
// a full range of configuration setups.
#define ML_TEST_NAME(platform_name, limit) ml_test_##platform_name##_##limit
#define ML_TEST_GEN(platform_name, test_case, limit) \
    static bool ML_TEST_NAME(platform_name, limit)(void*) { \
        BEGIN_TEST; \
        return test_runner(test_case, limit * MB); \
        END_TEST; \
    }

// Test a range of platforms and memory configurations
ML_TEST_GEN(nuc, test_cases[0], 32);
ML_TEST_GEN(nuc, test_cases[0], 64);
ML_TEST_GEN(nuc, test_cases[0], 96);
ML_TEST_GEN(nuc, test_cases[0], 128);
ML_TEST_GEN(nuc, test_cases[0], 256);
ML_TEST_GEN(nuc, test_cases[0], 512);
ML_TEST_GEN(nuc, test_cases[0], 1024);
ML_TEST_GEN(nuc, test_cases[0], 1536);
ML_TEST_GEN(nuc, test_cases[0], 2048);
ML_TEST_GEN(nuc, test_cases[0], 3072);
ML_TEST_GEN(nuc, test_cases[0], 4096);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 32);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 64);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 96);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 128);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 256);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 512);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 1024);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 1536);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 2048);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 3072);
ML_TEST_GEN(switch_alpha_12, test_cases[1], 4096);
ML_TEST_GEN(rpi3, test_cases[2], 32);
ML_TEST_GEN(rpi3, test_cases[2], 64);
ML_TEST_GEN(rpi3, test_cases[2], 96);
ML_TEST_GEN(rpi3, test_cases[2], 128);
ML_TEST_GEN(rpi3, test_cases[2], 256);
ML_TEST_GEN(rpi3, test_cases[2], 512);
ML_TEST_GEN(rpi3, test_cases[2], 1024);
ML_TEST_GEN(rpi3, test_cases[2], 1536);
ML_TEST_GEN(rpi3, test_cases[2], 2048);
ML_TEST_GEN(rpi3, test_cases[2], 3072);
ML_TEST_GEN(rpi3, test_cases[2], 4096);

#define ML_UNITTEST(platform_name, limit) \
    UNITTEST(#platform_name " " #limit "MB", ml_test_##platform_name##_##limit)
UNITTEST_START_TESTCASE(memlimit_tests)
UNITTEST("Test with an oversized ramdisk", ml_test_large_ramdisk)
ML_UNITTEST(nuc, 32)
ML_UNITTEST(nuc, 64)
ML_UNITTEST(nuc, 96)
ML_UNITTEST(nuc, 128)
ML_UNITTEST(nuc, 256)
ML_UNITTEST(nuc, 512)
ML_UNITTEST(nuc, 1024)
ML_UNITTEST(nuc, 1536)
ML_UNITTEST(nuc, 2048)
ML_UNITTEST(nuc, 3072)
ML_UNITTEST(nuc, 4096)
ML_UNITTEST(switch_alpha_12, 32)
ML_UNITTEST(switch_alpha_12, 64)
ML_UNITTEST(switch_alpha_12, 96)
ML_UNITTEST(switch_alpha_12, 128)
ML_UNITTEST(switch_alpha_12, 256)
ML_UNITTEST(switch_alpha_12, 512)
ML_UNITTEST(switch_alpha_12, 1024)
ML_UNITTEST(switch_alpha_12, 1536)
ML_UNITTEST(switch_alpha_12, 2048)
ML_UNITTEST(switch_alpha_12, 3072)
ML_UNITTEST(switch_alpha_12, 4096)
ML_UNITTEST(rpi3, 32)
ML_UNITTEST(rpi3, 64)
ML_UNITTEST(rpi3, 96)
ML_UNITTEST(rpi3, 128)
ML_UNITTEST(rpi3, 256)
ML_UNITTEST(rpi3, 512)
ML_UNITTEST(rpi3, 1024)
ML_UNITTEST(rpi3, 1536)
ML_UNITTEST(rpi3, 2048)
ML_UNITTEST(rpi3, 3072)
ML_UNITTEST(rpi3, 4096)
UNITTEST_END_TESTCASE(memlimit_tests, "memlim_tests", "Memory limit tests", nullptr, nullptr);
