// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <tuple>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using ParamType = std::tuple<TestFilesystemOptions, /*reuse_subdirectory=*/bool>;

class InodeReuseTest : public BaseFilesystemTest, public testing::WithParamInterface<ParamType> {
 public:
  InodeReuseTest() : BaseFilesystemTest(std::get<0>(GetParam())) {}

  bool reuse_subdirectory() const { return std::get<1>(GetParam()); }
};

// Try repeatedly creating and removing a file within a directory, as fast as possible, in an
// attempt to trigger filesystem-internal threading races between creation and deletion of a file.
TEST_P(InodeReuseTest, InodeReuse) {
  const std::string reuse = GetPath("reuse");
  ASSERT_EQ(mkdir(reuse.c_str(), 0755), 0);
  DIR* d = opendir(reuse.c_str());
  ASSERT_NE(d, nullptr);
  for (size_t i = 0; i < 1000; i++) {
    ASSERT_EQ(mkdirat(dirfd(d), "foo", 0666), 0);
    if (reuse_subdirectory()) {
      ASSERT_EQ(mkdirat(dirfd(d), "foo/bar", 0666), 0);
      ASSERT_EQ(unlinkat(dirfd(d), "foo/bar", 0), 0);
    }
    ASSERT_EQ(unlinkat(dirfd(d), "foo", 0), 0);
  }
  ASSERT_EQ(closedir(d), 0);
  ASSERT_EQ(rmdir(reuse.c_str()), 0);
}

// Return codes from helper threads
constexpr int kSuccess = 1;
constexpr int kFailure = -1;
constexpr int kUnexpectedFailure = -2;

using thrd_cb_t = int(void*);

class ThreadingTest : public FilesystemTest {
 protected:
  // Launch some threads, and have them all execute callback 'cb'.
  //
  // It is expected that:
  //  - kSuccessCount threads will return "kSuccess"
  //  - ALL OTHER threads will return "kFailure"
  //
  // In any other condition, this helper fails.  For example, returning "kUnexpectedFailure" from cb
  // is an easy way to fail the entire test from a background thread.
  template <size_t kNumThreads, size_t kSuccessCount>
  void ThreadActionTest(std::function<int()> cb) {
    static_assert(kNumThreads >= kSuccessCount, "Need more threads or less successes");

    thrd_t threads[kNumThreads];
    auto callback_wrapper = +[](void* arg) {
      auto cb = reinterpret_cast<std::function<int()>*>(arg);
      return (*cb)();
    };
    for (size_t i = 0; i < kNumThreads; i++) {
      ASSERT_EQ(thrd_create(&threads[i], callback_wrapper, &cb), thrd_success);
    }

    // Join all threads first before checking whether they were successful. This way all threads
    // will be cleaned up even if we encounter a failure.
    int success[kNumThreads];
    int result[kNumThreads];
    for (size_t i = 0; i < kNumThreads; i++) {
      success[i] = thrd_join(threads[i], &result[i]);
    };

    size_t success_count = 0;
    for (size_t i = 0; i < kNumThreads; i++) {
      ASSERT_EQ(success[i], thrd_success);
      if (result[i] == kSuccess) {
        success_count++;
        ASSERT_LE(success_count, kSuccessCount) << "Too many succeeding threads";
      } else {
        ASSERT_EQ(result[i], kFailure) << "Unexpected return code from worker thread";
      }
    }
    ASSERT_EQ(success_count, kSuccessCount) << "Not enough succeeding threads";
  }
};

constexpr size_t kIterCount = 10;

TEST_P(ThreadingTest, CreateUnlinkExclusive) {
  for (size_t i = 0; i < kIterCount; i++) {
    ASSERT_NO_FATAL_FAILURE((ThreadActionTest<10, 1>([this]() {
      fbl::unique_fd fd(open(GetPath("exclusive").c_str(), O_RDWR | O_CREAT | O_EXCL));
      if (fd) {
        return close(fd.release()) == 0 ? kSuccess : kUnexpectedFailure;
      } else if (errno == EEXIST) {
        return kFailure;
      }
      return kUnexpectedFailure;
    })));

    ASSERT_NO_FATAL_FAILURE((ThreadActionTest<10, 1>([this]() {
      if (unlink(GetPath("exclusive").c_str()) == 0) {
        return kSuccess;
      } else if (errno == ENOENT) {
        return kFailure;
      }
      return kUnexpectedFailure;
    })));
  }
}

TEST_P(ThreadingTest, MkdirRmdirExclusive) {
  for (size_t i = 0; i < kIterCount; i++) {
    ASSERT_NO_FATAL_FAILURE((ThreadActionTest<10, 1>([this]() {
      if (mkdir(GetPath("exclusive").c_str(), 0666) == 0) {
        return kSuccess;
      } else if (errno == EEXIST) {
        return kFailure;
      }
      return kUnexpectedFailure;
    })));

    ASSERT_NO_FATAL_FAILURE((ThreadActionTest<10, 1>([this]() {
      if (rmdir(GetPath("exclusive").c_str()) == 0) {
        return kSuccess;
      } else if (errno == ENOENT) {
        return kFailure;
      }
      return kUnexpectedFailure;
    })));
  }
}

TEST_P(ThreadingTest, RenameExclusive) {
  for (size_t i = 0; i < kIterCount; i++) {
    // Test case of renaming from a single source.
    ASSERT_EQ(mkdir(GetPath("rename_start").c_str(), 0666), 0);
    ASSERT_NO_FATAL_FAILURE((ThreadActionTest<10, 1>([this]() {
      if (rename(GetPath("rename_start").c_str(), GetPath("rename_end").c_str()) == 0) {
        return kSuccess;
      } else if (errno == ENOENT) {
        return kFailure;
      }
      return kUnexpectedFailure;
    })));
    ASSERT_EQ(rmdir(GetPath("rename_end").c_str()), 0);

    // Test case of renaming from multiple sources at once, to a single destination
    std::atomic<uint32_t> ctr{0};
    ASSERT_NO_FATAL_FAILURE((ThreadActionTest<10, 1>([this, &ctr]() {
      char start[128];
      snprintf(start, sizeof(start) - 1, GetPath("rename_start_%u").c_str(), ctr.fetch_add(1));
      if (mkdir(start, 0666)) {
        return kUnexpectedFailure;
      }

      // Give the target a child, so it cannot be overwritten as a target
      char child[256];
      snprintf(child, sizeof(child) - 1, "%s/child", start);
      if (mkdir(child, 0666)) {
        return kUnexpectedFailure;
      }

      if (rename(start, GetPath("rename_end").c_str()) == 0) {
        return kSuccess;
      } else if (errno == ENOTEMPTY || errno == EEXIST) {
        return rmdir(child) == 0 && rmdir(start) == 0 ? kFailure : kUnexpectedFailure;
      }
      return kUnexpectedFailure;
    })));

    DIR* dir = opendir(GetPath("rename_end").c_str());
    ASSERT_NE(dir, nullptr);
    struct dirent* de;
    while ((de = readdir(dir)) && de != nullptr) {
      unlinkat(dirfd(dir), de->d_name, AT_REMOVEDIR);
    }
    ASSERT_EQ(closedir(dir), 0);
    ASSERT_EQ(rmdir(GetPath("rename_end").c_str()), 0);
  }
}

TEST_P(ThreadingTest, RenameOverwrite) {
  for (size_t i = 0; i < kIterCount; i++) {
    // Test case of renaming from multiple sources at once, to a single destination
    std::atomic<uint32_t> ctr{0};
    ASSERT_NO_FATAL_FAILURE((ThreadActionTest<10, 10>([this, &ctr]() {
      char start[128];
      snprintf(start, sizeof(start) - 1, GetPath("rename_start_%u").c_str(), ctr.fetch_add(1));
      if (mkdir(start, 0666)) {
        return kUnexpectedFailure;
      }
      if (rename(start, GetPath("rename_end").c_str()) == 0) {
        return kSuccess;
      }
      return kUnexpectedFailure;
    })));
    ASSERT_EQ(rmdir(GetPath("rename_end").c_str()), 0);
  }
}

using ThreadingLinkTest = ThreadingTest;

TEST_P(ThreadingLinkTest, LinkExclusive) {
  for (size_t i = 0; i < kIterCount; i++) {
    fbl::unique_fd fd(open(GetPath("link_start").c_str(), O_RDWR | O_CREAT | O_EXCL));
    ASSERT_TRUE(fd);
    ASSERT_EQ(close(fd.release()), 0);

    ASSERT_NO_FATAL_FAILURE((ThreadActionTest<10, 1>([this]() {
      if (link(GetPath("link_start").c_str(), GetPath("link_end").c_str()) == 0) {
        return kSuccess;
      } else if (errno == EEXIST) {
        return kFailure;
      }
      return kUnexpectedFailure;
    })));

    ASSERT_EQ(unlink(GetPath("link_start").c_str()), 0);
    ASSERT_EQ(unlink(GetPath("link_end").c_str()), 0);

    if (fs().GetTraits().can_unmount) {
      EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
      EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
      EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
    }
  }
}

std::vector<TestFilesystemOptions> GetThreadingLinkTestCombinations() {
  std::vector<TestFilesystemOptions> test_combinations;
  for (TestFilesystemOptions options : AllTestFilesystems()) {
    if (options.filesystem->GetTraits().supports_hard_links) {
      test_combinations.push_back(options);
    }
  }
  return test_combinations;
}

std::string GetParamDescription(const testing::TestParamInfo<ParamType>& param) {
  std::stringstream s;
  s << std::get<0>(param.param) << (std::get<1>(param.param) ? "ReusingSubdir" : "");
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, InodeReuseTest,
                         testing::Combine(testing::ValuesIn(AllTestFilesystems()), testing::Bool()),
                         GetParamDescription);

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, ThreadingTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, ThreadingLinkTest,
                         testing::ValuesIn(GetThreadingLinkTestCombinations()),
                         testing::PrintToStringParamName());

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ThreadingLinkTest);

}  // namespace
}  // namespace fs_test
