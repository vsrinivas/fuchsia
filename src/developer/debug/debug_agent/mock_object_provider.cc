// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_object_provider.h"

#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/logging.h"

namespace debug_agent {

std::vector<zx::job> MockObjectProvider::GetChildJobs(zx_handle_t job_handle) const {
  zx_koid_t job_koid = static_cast<zx_koid_t>(job_handle);

  auto it = object_map_.find(job_koid);
  FXL_DCHECK(it != object_map_.end());
  MockJobObject* job = reinterpret_cast<MockJobObject*>(it->second);

  FXL_DCHECK(job) << "On koid: " << job_koid;
  FXL_DCHECK(job->type == MockObject::Type::kJob);

  std::vector<zx::job> child_jobs;
  for (auto& child_job : job->child_jobs) {
    child_jobs.push_back(zx::job(static_cast<zx_handle_t>(child_job->koid)));
  }

  return child_jobs;
}

std::vector<zx::process> MockObjectProvider::GetChildProcesses(zx_handle_t job_handle) const {
  zx_koid_t job_koid = static_cast<zx_koid_t>(job_handle);

  auto it = object_map_.find(job_koid);
  FXL_DCHECK(it != object_map_.end());

  MockJobObject* job = reinterpret_cast<MockJobObject*>(it->second);
  FXL_DCHECK(job) << "On koid: " << job_koid;
  FXL_DCHECK(job->type == MockObject::Type::kJob);

  std::vector<zx::process> child_processes;
  for (auto& child_process : job->child_processes) {
    child_processes.push_back(zx::process(static_cast<zx_handle_t>(child_process->koid)));
  }

  return child_processes;
}

std::string MockObjectProvider::NameForObject(zx_handle_t object_handle) const {
  zx_koid_t koid = static_cast<zx_koid_t>(object_handle);
  DEBUG_LOG(Test) << "Getting name for: " << object_handle;
  auto it = object_map_.find(koid);
  FXL_DCHECK(it != object_map_.end());

  DEBUG_LOG(Test) << "Getting name for " << object_handle << ", got " << it->second->name;

  return it->second->name;
}

zx_koid_t MockObjectProvider::KoidForObject(zx_handle_t object_handle) const {
  zx_koid_t koid = static_cast<zx_koid_t>(object_handle);
  auto it = object_map_.find(koid);
  FXL_DCHECK(it != object_map_.end());

  DEBUG_LOG(Test) << "Getting koid for " << object_handle << ", got " << it->second->koid;

  return it->second->koid;
};

zx::job MockObjectProvider::GetRootJob() const {
  return zx::job(GetRootJobKoid());
}

zx_koid_t MockObjectProvider::GetRootJobKoid() const {
  return root()->koid;
}

// Test Setup Implementation.

std::unique_ptr<MockObjectProvider> CreateDefaultMockObjectProvider() {
  auto provider = std::make_unique<MockObjectProvider>();
  MockJobObject* root = provider->AppendJob(nullptr, "root");

  provider->AppendProcess(root, "root-p1");
  provider->AppendProcess(root, "root-p2");
  provider->AppendProcess(root, "root-p3");

  MockJobObject* job1 = provider->AppendJob(root, "job1");
  provider->AppendProcess(job1, "job1-p1");
  provider->AppendProcess(job1, "job1-p2");

  MockJobObject* job11 = provider->AppendJob(job1, "job11");
  MockProcessObject* process = provider->AppendProcess(job11, "job11-p1");
  provider->AppendThread(process, "second-thread");

  MockJobObject* job12 = provider->AppendJob(job1, "job12");
  MockJobObject* job121 = provider->AppendJob(job12, "job121");
  provider->AppendProcess(job121, "job121-p1");
  process = provider->AppendProcess(job121, "job121-p2");
  provider->AppendThread(process, "second-thread");
  provider->AppendThread(process, "third-thread");

  return provider;
}

MockJobObject* MockObjectProvider::AppendJob(MockJobObject* job, std::string name) {
  if (job == nullptr) {
    root_ = CreateJob(name);
    return root_.get();
  }

  job->child_jobs.push_back(CreateJob(std::move(name)));
  return job->child_jobs.back().get();
}

MockProcessObject* MockObjectProvider::AppendProcess(MockJobObject* job, std::string name) {
  job->child_processes.push_back(CreateProcess(std::move(name)));

  // Create the initial thread.
  MockProcessObject* process = job->child_processes.back().get();
  process->child_threads.push_back(CreateThread());

  return process;
}

MockThreadObject* MockObjectProvider::AppendThread(MockProcessObject* process, std::string name) {
  process->child_threads.push_back(CreateThread(std::move(name)));
  return process->child_threads.back().get();
}

std::unique_ptr<MockJobObject> MockObjectProvider::CreateJob(std::string name) {
  int koid = next_koid_++;

  auto job = std::make_unique<MockJobObject>();
  job->koid = koid;
  job->name = std::move(name);
  job->type = MockObject::Type::kJob;

  object_map_[job->koid] = job.get();
  job_map_[job->name] = job.get();

  return job;
}

std::unique_ptr<MockProcessObject> MockObjectProvider::CreateProcess(std::string name) {
  int koid = next_koid_++;

  auto process = std::make_unique<MockProcessObject>();
  process->koid = koid;
  process->name = std::move(name);
  process->type = MockObject::Type::kProcess;

  object_map_[process->koid] = process.get();
  process_map_[process->name] = process.get();

  return process;
}

std::unique_ptr<MockThreadObject> MockObjectProvider::CreateThread(std::string name) {
  int koid = next_koid_++;

  auto thread = std::make_unique<MockThreadObject>();
  thread->koid = koid;
  thread->name = std::move(name);
  thread->type = MockObject::Type::kThread;

  object_map_[thread->koid] = thread.get();

  return thread;
}

MockObject* MockObjectProvider::ObjectByKoid(zx_koid_t koid) const {
  auto it = object_map_.find(koid);
  if (it == object_map_.end())
    return nullptr;
  return it->second;
}

const MockJobObject* MockObjectProvider::JobByName(const std::string& name) const {
  auto it = job_map_.find(name);
  if (it == job_map_.end())
    return nullptr;
  return it->second;
}

const MockProcessObject* MockObjectProvider::ProcessByName(const std::string& name) const {
  auto it = process_map_.find(name);
  if (it == process_map_.end())
    return nullptr;
  return it->second;
}

const MockThreadObject* MockProcessObject::GetThread(const std::string& thread_name) const {
  for (auto& thread : child_threads) {
    if (thread->name == thread_name)
      return thread.get();
  }

  return nullptr;
}

}  // namespace debug_agent
