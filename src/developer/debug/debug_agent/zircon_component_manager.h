// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_COMPONENT_MANAGER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_COMPONENT_MANAGER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/types.h>

#include <set>

#include "src/developer/debug/debug_agent/component_manager.h"
#include "src/developer/debug/debug_agent/system_interface.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_agent {

class ZirconComponentManager : public ComponentManager {
 public:
  ZirconComponentManager(SystemInterface* system_interface,
                         std::shared_ptr<sys::ServiceDirectory> services);
  ~ZirconComponentManager() override = default;

  // ComponentManager implementation.
  void SetDebugAgent(DebugAgent* debug_agent) override { debug_agent_ = debug_agent; }
  std::optional<debug_ipc::ComponentInfo> FindComponentInfo(zx_koid_t job_koid) const override;
  debug::Status LaunchComponent(const std::vector<std::string>& argv) override;
  debug::Status LaunchTest(std::string url, std::vector<std::string> case_filters) override;
  bool OnProcessStart(const ProcessHandle& process, StdioHandles* out_stdio,
                      std::string* process_name_override) override;

  // Handles an incoming component lifecycle event.
  void OnComponentEvent(fuchsia::sys2::Event event);

  // (For test only) Set the callback that will be invoked when the initialization is ready.
  // If the initialization is already done, callback will still be invoked in the message loop.
  void SetReadyCallback(fit::callback<void()> callback);

  auto GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  class TestLauncher;

  void GetNextComponentEvent();

  fit::callback<void()> ready_callback_ = []() {};

  DebugAgent* debug_agent_ = nullptr;  // nullable.

  std::shared_ptr<sys::ServiceDirectory> services_;

  // Information of all running components in the system, indexed by their job koids.
  std::map<zx_koid_t, debug_ipc::ComponentInfo> running_component_info_;
  fuchsia::sys2::EventStream2Ptr event_stream_binding_;

  // Monikers of v2 components we're expecting.
  // There's no way to set stdio handle for v2 components yet.
  std::set<std::string> expected_v2_components_;

  // The |running_tests_info_| is a mapping from the URLs to the test information.  The key of
  // |running_tests_info_| could be monikers, but the test framework doesn't provide them today.
  //
  // HOW TO ASSOCIATE PROCESSES WITH TEST CASES?
  //
  // Fuchsia test runners usually start one process for each test case, and each process has its
  // own stdout and stderr handles. So one test could correspond to many stdio handles.
  //
  // The test framework provides no functionality to associate the stdio with the process, i.e.,
  // it won't tell us the process koid for each test case. To associate outputs with processes,
  // we have to have some assumptions:
  //
  //   * The order of process starting events is the same as the order of test case identifiers.
  //   * The test runner will launch k+n processes, where n is the number of test cases, and the
  //     first k processes are used to inspect the test binary and list available test cases.
  //     As of the writing, k is 1 for gtest runner and k is 2 for rust test runner.
  //   * Gtest tests have "." in the case names and rust tests have "::" in the case names.
  //
  // These are the implementation detail about the test runners, but they are stable: as long as
  // test runners don't change their logic and launch test cases in the order of case identifiers,
  // the delivery of process starting events will be ordered without any flakiness.
  //
  // The overall lifecycle for launching a test will look like
  //
  //   * |TestLauncher::Launch()| inserts a new entry in |running_tests_info_|.
  //   * (a) |OnProcessStart()| receives process starting events for the first k processes.
  //   * For each test cases,
  //     (b) |TestLauncher::OnSuiteEvents()| receives |CaseFound| events, populates |case_names| and
  //         set |ignored_process|.
  //     (c) |OnProcessStart()| receives process starting events and populates |pids|.
  //         If |case_names| is available, it'll override the process name.
  //     (d) |TestLauncher::OnSuiteEvents()| receives |CaseArtifact| events that include the stdout
  //         or stderr handles. If the process is running, it'll |SetStdout| or |SetStderr|.
  //     (e) The process terminates.
  //
  // Only the order of events from the same channel, i.e., (a)(c)(e) or (b)(d), is determined.
  //
  // The worst case of our solution is everything comes out of order. We might
  //
  //   * Fail to attach to the first test case of a gtest because |CaseFound| arrives too late.
  //   * Fail to set the process name to the case name because |CaseFound| arrives too late.
  //   * Fail to set the stdout/stderr handle because the process has terminated.
  //
  // In either way, there won't be mismatch between the test output and the process.
  //
  // NOTE: It's not possible to inspect the handle table of the process and find the socket pair,
  // because the socket we get from test framework is not the opposite side sent to the process.
  //
  // TODO(fxbug.dev/107174): Use a better method to associate processes and test cases.
  struct TestInfo {
    size_t ignored_process = 2;           // number of processes not corresponding to test cases.
    std::vector<zx_koid_t> pids;          // koids of processes launched in the test.
    std::vector<std::string> case_names;  // names of test cases.
  };
  std::map<std::string, TestInfo> running_tests_info_;

  fxl::WeakPtrFactory<ZirconComponentManager> weak_factory_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_COMPONENT_MANAGER_H_
