// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <fs-test-utils/fixture.h>
#include <fs-test-utils/perftest.h>
#include <perftest/perftest.h>
#include <unittest/unittest.h>

namespace fs_bench {
namespace {

using fs_test_utils::Fixture;
using fs_test_utils::FixtureOptions;
using fs_test_utils::PerformanceTestOptions;
using fs_test_utils::TestCaseInfo;
using fs_test_utils::TestInfo;

constexpr uint8_t kMagicByte = 0xee;

constexpr int kWriteReadCycles = 3;

fbl::String GetBigFilePath(const Fixture& fixture) {
    fbl::String path = fbl::StringPrintf("%s/bigfile.txt", fixture.fs_path().c_str());
    return path;
}

bool WriteBigFile(ssize_t data_size, perftest::RepeatState* state, Fixture* fixture) {
    BEGIN_HELPER;

    fbl::unique_fd fd(open(GetBigFilePath(*fixture).c_str(), O_CREAT | O_WRONLY));
    ASSERT_TRUE(fd);
    state->DeclareStep("write");
    uint8_t data[data_size];
    // TODO(gevalentino): make kMagicByte random. Make Fixture take a seed parameter,
    // and use that seed to generate this value, then we pick the seed randomly by default,
    // or pass it via parameters(reproduceability) and errors need to log the seed
    // if the data depends on a randomized value.
    memset(data, kMagicByte, data_size);

    while (state->KeepRunning()) {
        ASSERT_EQ(write(fd.get(), data, data_size), data_size);
    }

    END_HELPER;
}

bool ReadBigFile(ssize_t data_size, perftest::RepeatState* state, Fixture* fixture) {
    BEGIN_HELPER;

    fbl::unique_fd fd(open(GetBigFilePath(*fixture).c_str(), O_RDONLY));
    ASSERT_TRUE(fd);
    state->DeclareStep("read");
    uint8_t data[data_size];

    while (state->KeepRunning()) {
        ASSERT_EQ(read(fd.get(), data, data_size), data_size);
        ASSERT_EQ(data[0], kMagicByte);
    }

    END_HELPER;
}

constexpr char kBaseComponent[] = "/aaa";

constexpr size_t kComponentLength = fbl::constexpr_strlen(kBaseComponent);

struct PathComponentGen {
    PathComponentGen() { memcpy(current, kBaseComponent, kComponentLength + 1); }

    // Advances current to the next component, following alphabetical order.
    // E.g: /aaa -> /aab ..../aaz -> /aba
    void Next() {
        for (int i = 3; i > 0; --i) {
            char next = static_cast<char>(static_cast<uint8_t>(current[i]) + 1);
            if (next > 'z') {
                current[i] = 'a';
            } else {
                current[i] = next;
                break;
            }
        }
    }
    // Add extra byte for null termination.
    char current[kComponentLength + 1];
};

bool PathWalkDown(const fbl::String& op_name, const fbl::Function<int(const char*)>& op,
                  perftest::RepeatState* state, Fixture* fixture,
                  fbl::StringBuffer<fs_test_utils::kPathSize>* path) {
    BEGIN_HELPER;
    PathComponentGen component;
    path->Append(fixture->fs_path());

    state->DeclareStep(op_name.c_str());
    state->DeclareStep("path_update");
    while (state->KeepRunning()) {
        path->Append(component.current);
        ASSERT_EQ(op(path->c_str()), 0);
        state->NextStep();
        component.Next();
    }
    END_HELPER;
}

bool PathWalkUp(const fbl::String& op_name, const fbl::Function<int(const char*)>& op,
                perftest::RepeatState* state, Fixture* fixture,
                fbl::StringBuffer<fs_test_utils::kPathSize>* path) {
    BEGIN_HELPER;
    state->DeclareStep(op_name.c_str());
    state->DeclareStep("path_update");
    while (state->KeepRunning() && *path != fixture->fs_path()) {
        ASSERT_EQ(op(path->c_str()), 0, path->c_str());
        state->NextStep();
        uint32_t new_size = static_cast<uint32_t>(path->length() - kComponentLength);
        path->Resize(new_size);
    }
    END_HELPER;
}

// Wrapper so state can be shared across calls.
class PathWalkOp {
public:
    PathWalkOp() = default;
    PathWalkOp(const PathWalkOp&) = delete;
    PathWalkOp(PathWalkOp&&) = delete;
    PathWalkOp& operator=(const PathWalkOp&) = delete;
    PathWalkOp& operator=(PathWalkOp&&) = delete;
    ~PathWalkOp() = default;

    // Will add components until |state::KeepGoing| returns false.
    bool Mkdir(perftest::RepeatState* state, Fixture* fixture) {
        path_.Clear();
        return PathWalkDown("mkdir", [](const char* path) { return mkdir(path, 0666); }, state,
                            fixture, &path_);
    }

    // Will stat components until |state::KeepGoing| returns false.
    bool Stat(perftest::RepeatState* state, Fixture* fixture) {
        path_.Clear();
        return PathWalkDown("stat",
                            [](const char* path) {
                                struct stat buff;
                                return stat(path, &buff);
                            },
                            state, fixture, &path_);
    }

    // Will unlink components until |state::KeepGoing| returns false.
    bool Unlink(perftest::RepeatState* state, Fixture* fixture) {
        return PathWalkUp("unlink", unlink, state, fixture, &path_);
    }

private:
    fbl::StringBuffer<fs_test_utils::kPathSize> path_;
};

} // namespace

bool RunBenchmark(int argc, char** argv) {
    FixtureOptions f_opts = FixtureOptions::Default(DISK_FORMAT_MINFS);
    PerformanceTestOptions p_opts;
    const int rw_test_sample_counts[] = {
        1024, 2048, 4096, 8192, 16384,
    };

    if (!fs_test_utils::ParseCommandLineArgs(argc, argv, &f_opts, &p_opts)) {
        return true;
    }

    fbl::Vector<TestCaseInfo> testcases;
    // Just do a single cycle for unittest mode.
    int cycles = (p_opts.is_unittest) ? 1 : kWriteReadCycles;
    // Read Write tests.
    for (int test_sample_count : rw_test_sample_counts) {
        TestCaseInfo testcase;
        testcase.sample_count = test_sample_count;
        testcase.name = fbl::StringPrintf("%s/Bigfile/16Kbytes/%d-Ops",
                                          disk_format_string_[f_opts.fs_type], test_sample_count);
        testcase.teardown = false;
        for (int cycle = 0; cycle < cycles; ++cycle) {
            TestInfo write_test, read_test;
            write_test.name =
                fbl::StringPrintf("%s/%dCycle/Write", testcase.name.c_str(), cycle + 1);
            write_test.test_fn = [](perftest::RepeatState* state, Fixture* fixture) {
                return WriteBigFile(16 * (1 << 10), state, fixture);
            };
            write_test.required_disk_space = test_sample_count * 16 * 1024;
            testcase.tests.push_back(fbl::move(write_test));

            read_test.name =
                fbl::StringPrintf("%s/%d-Cycle/Read", testcase.name.c_str(), cycle + 1);
            read_test.test_fn = [](perftest::RepeatState* state, Fixture* fixture) {
                return ReadBigFile(16 * (1 << 10), state, fixture);
            };
            read_test.required_disk_space = test_sample_count * 16 * 1024;
            testcase.tests.push_back(fbl::move(read_test));
        }
        testcases.push_back(fbl::move(testcase));
    }

    // Path walk tests.
    const int path_walk_sample_counts[] = {
        125,
        250,
        500,
        1000,
    };

    PathWalkOp pw_op;
    for (int test_sample_count : path_walk_sample_counts) {
        TestCaseInfo testcase;
        testcase.name = fbl::StringPrintf("%s/PathWalk/%d-Components",
                                          disk_format_string_[f_opts.fs_type], test_sample_count);
        testcase.sample_count = test_sample_count;
        testcase.teardown = false;

        TestInfo mkdir_test;
        mkdir_test.name = fbl::StringPrintf("%s/Mkdir", testcase.name.c_str());
        mkdir_test.test_fn = fbl::BindMember(&pw_op, &PathWalkOp::Mkdir);
        testcase.tests.push_back(fbl::move(mkdir_test));

        TestInfo stat_test;
        stat_test.name = fbl::StringPrintf("%s/Stat", testcase.name.c_str());
        stat_test.test_fn = fbl::BindMember(&pw_op, &PathWalkOp::Stat);
        testcase.tests.push_back(fbl::move(stat_test));

        TestInfo unlink_test;
        unlink_test.name = fbl::StringPrintf("%s/Unlink", testcase.name.c_str());
        unlink_test.test_fn = fbl::BindMember(&pw_op, &PathWalkOp::Unlink);

        testcase.tests.push_back(fbl::move(unlink_test));
        testcases.push_back(fbl::move(testcase));
    }

    return fs_test_utils::RunTestCases(f_opts, p_opts, testcases);
}
} // namespace fs_bench

int main(int argc, char** argv) {
    return fs_test_utils::RunWithMemFs(
        [argc, argv]() { return fs_bench::RunBenchmark(argc, argv) ? 0 : -1; });
}
