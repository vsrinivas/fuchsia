// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_OBJECT_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_OBJECT_PROVIDER_H_

#include <map>

#include "src/developer/debug/debug_agent/object_provider.h"

namespace debug_agent {

// These mock objects track fake koids. The ObjectProvider interface makes use of zx::objects that
// maintain the zx_handle_t lifetimes. In this tests, we use koids to act as "handles": If a
// MockProcessObject has koid 3, the value of the associated zx::process handle will be 3.
//
// Because the test most certainly DOES NOT have any open handle with those values, the only error
// that will come out of doing this is the zx_handle_close (called by the zx::object destructor)
// will error out with ZX_ERR_BAD_HANDLE, which is harmless.

struct MockJobObject;
struct MockProcessObject;
struct MockThreadObject;

struct MockObject {
  enum class Type {
    kJob,
    kProcess,
    kThread,
    kLast,
  };

  zx_koid_t koid;
  std::string name;
  Type type = Type::kLast;

  ~MockObject() = default;

  virtual MockJobObject* AsJob() { return nullptr; }
  virtual MockProcessObject* AsProcess() { return nullptr; }
  virtual MockThreadObject* AsThread() { return nullptr; }

  bool is_valid() const { return type != Type::kLast; }
};

struct MockThreadObject final : public MockObject {
  zx::thread GetHandle() const { return zx::thread(koid); }

  MockThreadObject* AsThread() override { return this; }
};

struct MockProcessObject final : public MockObject {
  std::vector<std::unique_ptr<MockThreadObject>> child_threads;

  MockProcessObject* AsProcess() override { return this; }

  zx::process GetHandle() const { return zx::process(koid); }
  const MockThreadObject* GetThread(const std::string& thread_name) const;
};

struct MockJobObject final : public MockObject {
  // Unique pointers so that they're fixed in memory and can cache the pointers.
  std::vector<std::unique_ptr<MockJobObject>> child_jobs;
  std::vector<std::unique_ptr<MockProcessObject>> child_processes;

  MockJobObject* AsJob() override { return this; }

  zx::job GetHandle() const { return zx::job(koid); }
};

// This objects permits to create your own job hierarchy using the |AppendJob| and |AppendProcess|
// methods. An already made hierarchy can be created out of the box by calling
// |CreateDefaultMockObjectProvider|.
class MockObjectProvider : public ObjectProvider {
 public:
  // Object Provider Interface.
  std::vector<zx::job> GetChildJobs(zx_handle_t job) const override;
  std::vector<zx::process> GetChildProcesses(zx_handle_t job) const override;

  std::vector<zx_koid_t> GetChildKoids(zx_handle_t parent, uint32_t child_kind) const override;

  zx_status_t GetChild(zx_handle_t parent, zx_koid_t koid, uint32_t rights,
                       zx_handle_t* child) const override;

  std::string NameForObject(zx_handle_t object) const override;
  zx_koid_t KoidForObject(zx_handle_t object) const override;

  // Meant to be called with handle objects (zx::process, zx::thread, etc.).
  template <typename T>
  std::string NameForObject(const T& handle) const {
    return NameForObject(handle.get());
  }
  template <typename T>
  zx_koid_t KoidForObject(const T& handle) const {
    return KoidForObject(handle.get());
  }

  zx::job GetRootJob() const override;
  zx_koid_t GetRootJobKoid() const override;

  zx_status_t Kill(zx_handle_t) override;

  MockJobObject* root() const { return root_.get(); }
  MockObject* ObjectByKoid(zx_koid_t koid) const;

  const MockJobObject* JobByName(const std::string& name) const;
  const MockProcessObject* ProcessByName(const std::string& name) const;

  // Passing |nullptr| to |parent_job| will create a root handle.
  MockJobObject* AppendJob(MockJobObject* parent_job, std::string name);
  MockProcessObject* AppendProcess(MockJobObject* parent_job, std::string name);
  MockThreadObject* AppendThread(MockProcessObject* parent_process, std::string name);

 private:
  std::unique_ptr<MockJobObject> CreateJob(std::string name);          // Advances the koid.
  std::unique_ptr<MockProcessObject> CreateProcess(std::string name);  // Advances the koid.
  std::unique_ptr<MockThreadObject> CreateThread(std::string name = "initial-thread");

  std::unique_ptr<MockJobObject> root_;

  std::map<zx_koid_t, MockObject*> object_map_;

  std::map<std::string, MockJobObject*> job_map_;
  std::map<std::string, MockProcessObject*> process_map_;

  uint64_t next_koid_ = 1;
};

// Creates a default process tree:
//
//  j: 1 root
//    p: 2 root-p1
//      t: 3 initial-thread
//    p: 4 root-p2
//      t: 5 initial-thread
//    p: 6 root-p3
//      t: 7 initial-thread
//    j: 8 job1
//      p: 9 job1-p1
//        t: 10 initial-thread
//      p: 11 job1-p2
//        t: 12 initial-thread
//      j: 13 job11
//        p: 14 job11-p1
//          t: 15 initial-thread
//          t: 16 second-thread
//      j: 17 job12
//        j: 18 job121
//          p: 19 job121-p1
//            t: 20 initial-thread
//          p: 21 job121-p2
//            t: 22 initial-thread
//            t: 22 second-thread
//            t: 23 third-thread
void FillInMockObjectProvider(MockObjectProvider*);

// Creates a new MockObjectProvider and calls |FillInMockObjectProvider|.
std::unique_ptr<MockObjectProvider> CreateDefaultMockObjectProvider();

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_OBJECT_PROVIDER_H_
