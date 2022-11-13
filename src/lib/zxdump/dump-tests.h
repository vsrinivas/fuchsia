// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_DUMP_TESTS_H_
#define SRC_LIB_ZXDUMP_DUMP_TESTS_H_

#include <lib/fdio/spawn.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zxdump/dump.h>
#include <lib/zxdump/fd-writer.h>
#include <lib/zxdump/task.h>
#include <lib/zxdump/zstd-writer.h>
#include <unistd.h>

#include <cstdio>
#include <vector>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace zxdump::testing {

constexpr time_t kNoDate = 0;           // Value for no date recorded.
constexpr time_t kTestDate = 74697240;  // Long, long ago.

// A simple test program starts up and waits.
class TestProcess {
 public:
  zx::unowned_process borrow() const { return zx::unowned_process{process_}; }

  LiveTask handle() const {
    zx::process dup;
    EXPECT_EQ(ZX_OK, process_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    return dup;
  }

  ~TestProcess() {
    if (process_) {
      EXPECT_EQ(ZX_OK, process_.kill());
    }
    if (job_ && kill_job_) {
      EXPECT_EQ(ZX_OK, job_.kill());
    }
  }

  TestProcess& SpawnAction(const fdio_spawn_action_t& action) {
    spawn_actions_.push_back(action);
    return *this;
  }

  void StartChild(std::vector<const char*> args = {}) {
    args.insert(args.begin(), kChildProgram);
    args.push_back(nullptr);
    ASSERT_FALSE(*std::prev(args.end()));
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH] = "";
    ASSERT_EQ(fdio_spawn_etc(job().get(), FDIO_SPAWN_CLONE_ALL, kChildProgram, args.data(), environ,
                             spawn_actions_.size(), spawn_actions_.data(),
                             process_.reset_and_get_address(), err_msg),
              ZX_OK)
        << err_msg;
  }

  const zx::process& process() const { return process_; }

  zx_koid_t koid() const { return GetKoid(process_); }

  // Explicitly choose the job to use.
  void set_job(zx::job&& job, bool kill_job = false) {
    job_ = std::move(job);
    kill_job_ = kill_job;
  }
  void set_job(const zx::job& job) { ASSERT_EQ(ZX_OK, job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_)); }

  // This returns the job StartChild will launch the test process in.
  // If set_job hasn't been called, it just uses the default job.
  const zx::job& job() {
    if (!job_) {
      return *default_job_;
    }
    return job_;
  }

  zx_koid_t job_koid() const { return GetKoid(job_); }

  // Create a new empty job and set_job() to that.
  void HermeticJob(const zx::job& parent = *zx::job::default_job()) {
    ASSERT_FALSE(job_);
    ASSERT_EQ(ZX_OK, zx::job::create(parent, 0, &job_));
    kill_job_ = true;
  }

  // This is a standard SegmentCallback that can be used.
  static fit::result<zxdump::Error, zxdump::SegmentDisposition> PruneAllMemory(
      zxdump::SegmentDisposition segment, const zx_info_maps_t& maps, const zx_info_vmo_t& vmo) {
    segment.filesz = 0;
    return fit::ok(segment);
  }

 private:
  static constexpr const char* kChildProgram = "/pkg/bin/zxdump-test-child";

  template <typename Handle>
  static zx_koid_t GetKoid(const Handle& object) {
    zx_info_handle_basic_t info = {.koid = ZX_KOID_INVALID};
    EXPECT_EQ(ZX_OK, object.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
    return info.koid;
  }

  zx::unowned_job default_job_ = zx::job::default_job();
  std::vector<fdio_spawn_action_t> spawn_actions_;
  zx::process process_;
  zx::job job_;
  bool kill_job_ = false;
};

using PrecollectFunction = fit::function<void(zxdump::ProcessDump<zx::unowned_process>& dump)>;

class TestProcessForPropertiesAndInfo : public TestProcess {
 public:
  // Start a child for basic property & info dump testing.
  void StartChild();

  // Do the basic dump using the dumper API.
  template <typename Writer>
  void Dump(
      Writer& writer,
      PrecollectFunction precollect = [](zxdump::ProcessDump<zx::unowned_process>& dump) {});

  // Verify a dump file for that child was inserted and looks right.
  void CheckDump(zxdump::TaskHolder& holder, bool threads_dumped = true);

 private:
  static constexpr const char* kChildName = "zxdump-property-test-child";
};

// The template and its instantiations are defined in dump-tests.cc.
extern template void TestProcessForPropertiesAndInfo::Dump(FdWriter&, PrecollectFunction);
extern template void TestProcessForPropertiesAndInfo::Dump(ZstdWriter&, PrecollectFunction);

class TestProcessForSystemInfo : public TestProcessForPropertiesAndInfo {
 public:
  // Start a child for system information dump testing.
  void StartChild();

  // Do the basic dump using the dumper API.
  template <typename Writer>
  void Dump(Writer& writer) {
    TestProcessForPropertiesAndInfo::Dump(writer, Precollect);
  }

  // Verify a dump file for that child was inserted and looks right.
  void CheckDump(zxdump::TaskHolder& holder);

 private:
  static constexpr const char* kChildName = "zxdump-system-test-child";

  static void Precollect(zxdump::ProcessDump<zx::unowned_process>& dump) {
    auto result = dump.CollectSystem();
    ASSERT_TRUE(result.is_ok()) << result.error_value();
  }
};

class TestProcessForKernelInfo : public TestProcessForPropertiesAndInfo {
 public:
  // Start a child for privileged kernel information dump testing.
  void StartChild();

  // Do the basic dump using the dumper API.
  template <typename Writer>
  void Dump(Writer& writer) {
    auto precollect = [this](auto& dump) { Precollect(dump); };
    TestProcessForPropertiesAndInfo::Dump(writer, precollect);
  }

  // Verify a dump file for that child was inserted and looks right.
  void CheckDump(zxdump::TaskHolder& holder);

  const zx::resource& root_resource() const { return root_resource_; }

 private:
  static constexpr const char* kChildName = "zxdump-kernel-test-child";

  void Precollect(zxdump::ProcessDump<zx::unowned_process>& dump);

  zx::resource root_resource_;
};

}  // namespace zxdump::testing

#endif  // SRC_LIB_ZXDUMP_DUMP_TESTS_H_
