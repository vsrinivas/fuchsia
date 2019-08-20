// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_job.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/object_util.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

// This test uses a set of mock objects that track fake koids. The ObjectProvider interface makes
// use of zx::objects that maintain the zx_handle_t lifetimes. In this tests, we use koids to act
// as "handles": If a FakeProcess has koid 3, the value of the associated zx::process underlying
// handle will be 3.
//
// Because the test most certainly DOES NOT have any open handle with those values, the only error
// that will come out of doing this is the zx_handle_close (called by the zx::object destructor)
// will error out with ZX_ERR_BAD_HANDLE, which is harmless.

struct MockObject {
  enum class Type {
    kJob,
    kProcess,
  };

  zx_koid_t koid;
  std::string name;
  Type type;
};

struct FakeProcess : public MockObject {};

struct FakeJob : public MockObject {
  // Unique pointers so that they're fixed in memory and can cache the pointers.
  std::vector<std::unique_ptr<FakeJob>> child_jobs;
  std::vector<std::unique_ptr<FakeProcess>> child_processes;
};

class MockObjectProvider : public ObjectProvider {
 public:
   MockObjectProvider() {
     CreateJobTree();
   }

   std::vector<zx::job> GetChildJobs(zx_handle_t job) override;
   std::vector<zx::process> GetChildProcesses(zx_handle_t job) override;

   std::string NameForObject(zx_handle_t object) override;
   zx_koid_t KoidForObject(zx_handle_t object) override;

   const FakeJob& root() const { return *root_; }

   MockObject* ObjectByKoid(zx_koid_t koid) const;
   MockObject* ObjectByName(const std::string& name) const;

 private:
  void CreateJobTree();

  FakeJob* AppendJob(FakeJob*, std::string name);
  void AppendProcess(FakeJob*, std::string name);

  std::unique_ptr<FakeJob> CreateJob(std::string name);            // Advances the koid.
  std::unique_ptr<FakeProcess> CreateProcess(std::string name);    // Advances the koid.

  std::unique_ptr<FakeJob> root_;
  std::map<zx_koid_t, MockObject*> object_map_;   // For easy access.
  std::map<std::string, MockObject*> name_map_;   // For easy access.

  uint64_t next_koid_ = 1;
};

class MockProcessStartHandler : public ProcessStartHandler {
 public:
  void OnProcessStart(const std::string& filter, zx::process process) {}
};

bool IsProcessMatched(const MockObjectProvider& provider, const std::set<zx_koid_t>& matches,
                      const std::string& process_name) {
  auto process = provider.ObjectByName(process_name);
  FXL_DCHECK(process) << "Could not find mock process " << process_name;
  return matches.find(process->koid) != matches.end();
}

std::string PrintMatchedKoids(const MockObjectProvider& provider,
                              const std::set<zx_koid_t>& matches) {
  std::stringstream ss;
  ss << "Matched koids: ";
  for (zx_koid_t koid : matches) {
    ss << koid;
    auto process = provider.ObjectByKoid(koid);
    if (process) {
      ss << " (" << process->name << ")";
    } else {
      ss << " (<NOT FOUND>)";
    }
    ss << ", ";
  }

  return ss.str();
}

// Tests -------------------------------------------------------------------------------------------

TEST(DebuggedJob, NoMatch) {
  MockObjectProvider provider;
  MockProcessStartHandler start_handler;
  const FakeJob& root = provider.root();
  DebuggedJob job(&provider, &start_handler, root.koid, zx::job(root.koid));

  auto matches = job.SetFilters({"no-match"});

  EXPECT_TRUE(matches.empty());
}

TEST(DebuggedJob, SingleMatch) {
  MockObjectProvider provider;
  MockProcessStartHandler start_handler;
  const FakeJob& root = provider.root();
  DebuggedJob job(&provider, &start_handler, root.koid, zx::job(root.koid));

  auto matches = job.SetFilters({"root-p1"});

  ASSERT_EQ(matches.size(), 1u) << PrintMatchedKoids(provider, matches);
  EXPECT_TRUE(IsProcessMatched(provider, matches, "root-p1"));
}

TEST(DebuggedJob, MultipleMatches) {
  MockObjectProvider provider;
  MockProcessStartHandler start_handler;
  const FakeJob& root = provider.root();
  DebuggedJob job(&provider, &start_handler, root.koid, zx::job(root.koid));

  auto matches = job.SetFilters({"job121"});

  ASSERT_EQ(matches.size(), 2u) << PrintMatchedKoids(provider, matches);
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job121-p1"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job121-p2"));
}

TEST(DebuggedJob, MultipleFilters) {
  MockObjectProvider provider;
  MockProcessStartHandler start_handler;
  const FakeJob& root = provider.root();
  DebuggedJob job(&provider, &start_handler, root.koid, zx::job(root.koid));

  auto matches = job.SetFilters({"job11", "job12", "root"});

  ASSERT_EQ(matches.size(), 6u) << PrintMatchedKoids(provider, matches);
  EXPECT_TRUE(IsProcessMatched(provider, matches, "root-p1"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "root-p2"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "root-p3"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job11-p1"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job121-p1"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job121-p2"));
}

TEST(DebuggedJob, SubJobMatching) {
  MockObjectProvider provider;
  MockProcessStartHandler start_handler;
  const FakeJob& root = provider.root();

  // Taking from the root.
  DebuggedJob job(&provider, &start_handler, root.koid, zx::job(root.koid));
  auto matches = job.SetFilters({"p1"});

  ASSERT_EQ(matches.size(), 4u) << PrintMatchedKoids(provider, matches);
  EXPECT_TRUE(IsProcessMatched(provider, matches, "root-p1"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job1-p1"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job11-p1"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job121-p1"));

  MockObject* fake_sub_job = provider.ObjectByName("job1");
  ASSERT_TRUE(fake_sub_job);

  DebuggedJob sub_job(&provider, &start_handler, fake_sub_job->koid, zx::job(fake_sub_job->koid));
  matches = sub_job.SetFilters({"p1"});

  ASSERT_EQ(matches.size(), 3u) << PrintMatchedKoids(provider, matches);
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job1-p1"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job11-p1"));
  EXPECT_TRUE(IsProcessMatched(provider, matches, "job121-p1"));
}

// MockObjectProvider Implementation ---------------------------------------------------------------

// ObjectProvider Implementation.

std::vector<zx::job> MockObjectProvider::GetChildJobs(zx_handle_t job_handle) {
  zx_koid_t job_koid = static_cast<zx_koid_t>(job_handle);
  FakeJob* job = reinterpret_cast<FakeJob*>(object_map_[job_koid]);
  FXL_DCHECK(job) << "On koid: " << job_koid;
  FXL_DCHECK(job->type == MockObject::Type::kJob);

  std::vector<zx::job> child_jobs;
  for (auto& child_job : job->child_jobs) {
    child_jobs.push_back(zx::job(static_cast<zx_handle_t>(child_job->koid)));
  }

  return child_jobs;
}

std::vector<zx::process> MockObjectProvider::GetChildProcesses(zx_handle_t job_handle) {
  zx_koid_t job_koid = static_cast<zx_koid_t>(job_handle);
  FakeJob* job = reinterpret_cast<FakeJob*>(object_map_[job_koid]);
  FXL_DCHECK(job) << "On koid: " << job_koid;
  FXL_DCHECK(job->type == MockObject::Type::kJob);

  std::vector<zx::process> child_processes;
  for (auto& child_process : job->child_processes) {
    child_processes.push_back(zx::process(static_cast<zx_handle_t>(child_process->koid)));
  }

  return child_processes;
}

std::string MockObjectProvider::NameForObject(zx_handle_t object_handle) {
  zx_koid_t koid = static_cast<zx_koid_t>(object_handle);
  DEBUG_LOG(Test) << "Getting name for: " << object_handle;
  MockObject* object = object_map_[koid];
  FXL_DCHECK(object) << "No object for " << object_handle;

  DEBUG_LOG(Test) << "Getting name for " << object_handle << ", got " << object->name;

  return object->name;
}

zx_koid_t MockObjectProvider::KoidForObject(zx_handle_t object_handle) {
  zx_koid_t koid = static_cast<zx_koid_t>(object_handle);
  MockObject* object = object_map_[koid];
  FXL_DCHECK(object) << "No object for " << object_handle;

  DEBUG_LOG(Test) << "Getting koid for " << object_handle << ", got " << object->koid;

  return object->koid;
};

// Test Setup Implementation.

// Tree is:
//
//  j: root
//    p: root-p1
//    p: root-p2
//    p: root-p3
//    j: job1
//      p: job1-p1
//      p: job1-p2
//      j: job11
//        p: job11-p1
//      j: job12
//        j: job121
//          p: job121-p1
//          p: job121-p2
//
void MockObjectProvider::CreateJobTree() {
  root_ = CreateJob("root");

  AppendProcess(root_.get(), "root-p1");
  AppendProcess(root_.get(), "root-p2");
  AppendProcess(root_.get(), "root-p3");

  FakeJob* job1 = AppendJob(root_.get(), "job1");
  AppendProcess(job1, "job1-p1");
  AppendProcess(job1, "job1-p2");

  FakeJob* job11 = AppendJob(job1, "job11");
  AppendProcess(job11, "job11-p1");   // process-6

  FakeJob* job12= AppendJob(job1, "job12");     // job-3
  FakeJob* job121 = AppendJob(job12, "job121");   // job-4
  AppendProcess(job121, "job121-p1");  // process-7
  AppendProcess(job121, "job121-p2");  // process-8
}

FakeJob* MockObjectProvider::AppendJob(FakeJob* job, std::string name) {
  job->child_jobs.push_back(CreateJob(std::move(name)));
  return job->child_jobs.back().get();
}

void MockObjectProvider::AppendProcess(FakeJob* job, std::string name) {
  job->child_processes.push_back(CreateProcess(std::move(name)));
}

std::unique_ptr<FakeJob> MockObjectProvider::CreateJob(std::string name) {
  int koid = next_koid_++;

  auto job = std::make_unique<FakeJob>();
  job->koid = koid;
  job->name = std::move(name);
  job->type = MockObject::Type::kJob;

  object_map_[job->koid] = job.get();
  name_map_[job->name] = job.get();

  return job;
}

std::unique_ptr<FakeProcess> MockObjectProvider::CreateProcess(std::string name) {
  int koid = next_koid_++;

  auto process = std::make_unique<FakeProcess>();
  process->koid = koid;
  process->name = std::move(name);
  process->type = MockObject::Type::kProcess;

  object_map_[process->koid] = process.get();
  name_map_[process->name] = process.get();

  return process;
}

MockObject* MockObjectProvider::ObjectByKoid(zx_koid_t koid) const {
  auto it = object_map_.find(koid);
  if (it == object_map_.end())
    return nullptr;
  return it->second;
}

MockObject* MockObjectProvider::ObjectByName(const std::string& name) const {
  auto it = name_map_.find(name);
  if (it == name_map_.end())
    return nullptr;
  return it->second;
}

}  // namespace

}  // namespace debug_agent
