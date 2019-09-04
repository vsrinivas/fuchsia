// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#pragma once

#include "src/developer/debug/debug_agent/object_provider.h"

#include <map>

namespace debug_agent {

// These mock objects track fake koids. The ObjectProvider interface makes use of zx::objects that
// maintain the zx_handle_t lifetimes. In this tests, we use koids to act as "handles": If a
// MockProcessObject has koid 3, the value of the associated zx::process handle will be 3.
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

struct MockProcessObject : public MockObject {};

struct MockJobObject : public MockObject {
  // Unique pointers so that they're fixed in memory and can cache the pointers.
  std::vector<std::unique_ptr<MockJobObject>> child_jobs;
  std::vector<std::unique_ptr<MockProcessObject>> child_processes;
};

// This objects permits to create your own job hierarchy using the |AppendJob| and |AppendProcess|
// methods. An already made hierarchy can be created out of the box by calling
// |CreateDefaultMockObjectProvider|.
class MockObjectProvider : public ObjectProvider {
 public:
   // Object Provider Interface.
   std::vector<zx::job> GetChildJobs(zx_handle_t job) override;
   std::vector<zx::process> GetChildProcesses(zx_handle_t job) override;

   std::string NameForObject(zx_handle_t object) override;
   zx_koid_t KoidForObject(zx_handle_t object) override;

   MockJobObject* root() const { return root_.get(); }
   MockObject* ObjectByKoid(zx_koid_t koid) const;
   MockObject* ObjectByName(const std::string& name) const;

   // Passing |nullptr| to |parent_job| will create a root handle.
   MockJobObject* AppendJob(MockJobObject* parent_job, std::string name);

   MockProcessObject* AppendProcess(MockJobObject* parent_job, std::string name);

  private:
   std::unique_ptr<MockJobObject> CreateJob(std::string name);          // Advances the koid.
   std::unique_ptr<MockProcessObject> CreateProcess(std::string name);  // Advances the koid.

   std::unique_ptr<MockJobObject> root_;
   std::map<zx_koid_t, MockObject*> object_map_;  // For easy access.
   std::map<std::string, MockObject*> name_map_;  // For easy access.

   uint64_t next_koid_ = 1;
};


// Creates a default process tree:
//
//  j: 1 root
//    p: 2 root-p1
//    p: 3 root-p2
//    p: 4 root-p3
//    j: 5 job1
//      p: 6 job1-p1
//      p: 7 job1-p2
//      j: 8 job11
//        p: 9 job11-p1
//      j: 10 job12
//        j: 11 job121
//          p: 12 job121-p1
//          p: 13 job121-p2
MockObjectProvider CreateDefaultMockObjectProvider();

}  // namespace debug_agent
