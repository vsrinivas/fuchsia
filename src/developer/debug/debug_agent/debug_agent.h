// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_AGENT_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_AGENT_H_

#include <zircon/types.h>

#include <map>
#include <memory>

#include "gtest/gtest_prod.h"
#include "src/developer/debug/debug_agent/agent_configuration.h"
#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/component_launcher.h"
#include "src/developer/debug/debug_agent/debugged_job.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/limbo_provider.h"
#include "src/developer/debug/debug_agent/remote_api.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_agent {

class SystemInterface;

// Main state and control for the debug agent.
class DebugAgent : public RemoteAPI,
                   public ProcessStartHandler,
                   public Breakpoint::ProcessDelegate {
 public:
  // A MessageLoopZircon should already be set up on the current thread.
  //
  // The stream must outlive this class. It will be used to send data to the
  // client. It will not be read (that's the job of the provider of the
  // RemoteAPI).
  explicit DebugAgent(std::unique_ptr<SystemInterface> system_interface);
  ~DebugAgent();

  fxl::WeakPtr<DebugAgent> GetWeakPtr();

  SystemInterface& system_interface() { return *system_interface_; }

  // Connects the debug agent to a stream buffer.
  // The buffer can be disconnected and the debug agent will remain intact until the moment a new
  // buffer is connected and messages start flowing through again.
  void Connect(debug_ipc::StreamBuffer*);
  void Disconnect();

  debug_ipc::StreamBuffer* stream();

  void RemoveDebuggedProcess(zx_koid_t process_koid);

  void RemoveDebuggedJob(zx_koid_t job_koid);

  Breakpoint* GetBreakpoint(uint32_t breakpoint_id);
  void RemoveBreakpoint(uint32_t breakpoint_id);

  // ProcessStartHandler implementation.
  void OnProcessStart(const std::string& filter, std::unique_ptr<ProcessHandle> process) override;

  void InjectProcessForTest(std::unique_ptr<DebuggedProcess> process);

  bool should_quit() const { return configuration_.quit_on_exit; }

  DebuggedJob* GetDebuggedJob(zx_koid_t koid);
  DebuggedProcess* GetDebuggedProcess(zx_koid_t koid);
  DebuggedThread* GetDebuggedThread(zx_koid_t process_koid, zx_koid_t thread_koid);

  // Returns the exception handling strategy for a given type.
  debug_ipc::ExceptionStrategy GetExceptionStrategy(debug_ipc::ExceptionType type);

  // RemoteAPI implementation.
  void OnConfigAgent(const debug_ipc::ConfigAgentRequest& request,
                     debug_ipc::ConfigAgentReply* reply) override;
  void OnHello(const debug_ipc::HelloRequest& request, debug_ipc::HelloReply* reply) override;
  void OnStatus(const debug_ipc::StatusRequest& request, debug_ipc::StatusReply* reply) override;
  void OnLaunch(const debug_ipc::LaunchRequest& request, debug_ipc::LaunchReply* reply) override;
  void OnKill(const debug_ipc::KillRequest& request, debug_ipc::KillReply* reply) override;
  void OnAttach(std::vector<char> serialized) override;
  // |transaction_id| is the id of the IPC message.
  void OnAttach(uint32_t transaction_id, const debug_ipc::AttachRequest&) override;
  void OnDetach(const debug_ipc::DetachRequest& request, debug_ipc::DetachReply* reply) override;
  void OnPause(const debug_ipc::PauseRequest& request, debug_ipc::PauseReply* reply) override;
  void OnQuitAgent(const debug_ipc::QuitAgentRequest& request,
                   debug_ipc::QuitAgentReply* reply) override;
  void OnResume(const debug_ipc::ResumeRequest& request, debug_ipc::ResumeReply* reply) override;
  void OnModules(const debug_ipc::ModulesRequest& request, debug_ipc::ModulesReply* reply) override;
  void OnProcessTree(const debug_ipc::ProcessTreeRequest& request,
                     debug_ipc::ProcessTreeReply* reply) override;
  void OnThreads(const debug_ipc::ThreadsRequest& request, debug_ipc::ThreadsReply* reply) override;
  void OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                    debug_ipc::ReadMemoryReply* reply) override;
  void OnReadRegisters(const debug_ipc::ReadRegistersRequest& request,
                       debug_ipc::ReadRegistersReply* reply) override;
  void OnWriteRegisters(const debug_ipc::WriteRegistersRequest& request,
                        debug_ipc::WriteRegistersReply* reply) override;
  void OnAddOrChangeBreakpoint(const debug_ipc::AddOrChangeBreakpointRequest& request,
                               debug_ipc::AddOrChangeBreakpointReply* reply) override;
  void OnRemoveBreakpoint(const debug_ipc::RemoveBreakpointRequest& request,
                          debug_ipc::RemoveBreakpointReply* reply) override;
  void OnSysInfo(const debug_ipc::SysInfoRequest& request, debug_ipc::SysInfoReply* reply) override;
  void OnProcessStatus(const debug_ipc::ProcessStatusRequest& request,
                       debug_ipc::ProcessStatusReply* reply) override;
  void OnThreadStatus(const debug_ipc::ThreadStatusRequest& request,
                      debug_ipc::ThreadStatusReply* reply) override;
  void OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                      debug_ipc::AddressSpaceReply* reply) override;
  void OnJobFilter(const debug_ipc::JobFilterRequest& request,
                   debug_ipc::JobFilterReply* reply) override;
  void OnWriteMemory(const debug_ipc::WriteMemoryRequest& request,
                     debug_ipc::WriteMemoryReply* reply) override;
  void OnLoadInfoHandleTable(const debug_ipc::LoadInfoHandleTableRequest& request,
                             debug_ipc::LoadInfoHandleTableReply* reply) override;
  void OnUpdateGlobalSettings(const debug_ipc::UpdateGlobalSettingsRequest& request,
                              debug_ipc::UpdateGlobalSettingsReply* reply) override;

 private:
  FRIEND_TEST(DebugAgentTests, Kill);

  // Breakpoint::ProcessDelegate implementation ----------------------------------------------------

  zx_status_t RegisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) override;
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) override;
  void SetupBreakpoint(const debug_ipc::AddOrChangeBreakpointRequest& request,
                       debug_ipc::AddOrChangeBreakpointReply* reply);

  zx_status_t RegisterWatchpoint(Breakpoint* bp, zx_koid_t process_koid,
                                 const debug_ipc::AddressRange& range) override;
  void UnregisterWatchpoint(Breakpoint* bp, zx_koid_t process_koid,
                            const debug_ipc::AddressRange& range) override;

  // Job/Process/Thread Management -----------------------------------------------------------------

  zx_status_t AddDebuggedJob(std::unique_ptr<JobHandle> job);
  zx_status_t AddDebuggedProcess(DebuggedProcessCreateInfo&&, DebuggedProcess** added);

  // Attempts to attach to the given process and sends a AttachReply message
  // to the client with the result.
  void AttachToProcess(zx_koid_t process_koid, uint32_t transaction_id);
  zx_status_t AttachToLimboProcess(zx_koid_t process_koid, uint32_t transaction_id);
  zx_status_t AttachToExistingProcess(zx_koid_t process_koid, uint32_t transaction_id);

  void LaunchProcess(const debug_ipc::LaunchRequest&, debug_ipc::LaunchReply*);

  void LaunchComponent(const debug_ipc::LaunchRequest&, debug_ipc::LaunchReply*);
  void OnComponentTerminated(int64_t return_code, const ComponentDescription& description,
                             fuchsia::sys::TerminationReason reason);

  // Process Limbo ---------------------------------------------------------------------------------

  void OnProcessEnteredLimbo(const LimboProvider::Record& record);

  // Members ---------------------------------------------------------------------------------------

  std::unique_ptr<SystemInterface> system_interface_;

  debug_ipc::StreamBuffer* stream_ = nullptr;

  std::map<zx_koid_t, std::unique_ptr<DebuggedJob>> jobs_;
  std::map<zx_koid_t, std::unique_ptr<DebuggedProcess>> procs_;

  // Processes obtained through limbo do not have the ZX_RIGHT_DESTROY right, so cannot be killed
  // by the debugger. Instead what we do is detach from them and mark those as "waiting to be
  // killed". When they re-enter the limbo, the debugger will then detect that is one of those
  // processes and release it from limbo, effectively killing it.
  //
  // NOTE(01/2020): The reason for this is because the limbo gets the exception from crashsvc,
  //                which has an exception channel upon the root job handle. Exceptions obtained
  //                through an exception channel will hold the same rights as the origin handle (the
  //                root job one in this case). Now, the root job handle svchost receives doesn't
  //                have the ZX_RIGHT_DESTROY right, as you don't really want to be able to kill
  //                the root job. This means that all the handles obtained through an exception
  //                will not have the destroy right, thus making the debugger jump through this
  //                hoop.
  //
  //                See src/bringup/bin/svchost/crashsvc.cc for more details.
  //
  //                Note that if the situation changes and the svchost actually gets destroy rights
  //                on the exception channel, that situation will seamlessly work here. This is
  //                because the debug agent will only track "limbo killed processes" if trying
  //                to kill results in ZX_ERR_ACCESS_DENIED *and* they came from limbo. If the limbo
  //                process handles now have destroy rights, killing them will work, thus skipping
  //                this dance.
  std::set<zx_koid_t> killed_limbo_procs_;

  std::map<uint32_t, Breakpoint> breakpoints_;

  // Normally the debug agent would be attached to the root job or the
  // component root job and give the client the koid. This is a job koid needed
  // to be able to create an invisible filter to catch the newly started
  // component. This will be 0 if not attached to such a job.
  // TODO(donosoc): Hopefully we could get the created job for the component
  //                so we can only filter on that.
  zx_koid_t attached_root_job_koid_ = 0;

  // Each component launch is assigned an unique filter and id. This is because
  // new components are attached via the job filter mechanism. When a particular
  // filter attached, we use this id to know which component launch just
  // happened and we can communicate it to the client.
  struct ExpectedComponent {
    ComponentDescription description;
    ComponentHandles handles;
    fuchsia::sys::ComponentControllerPtr controller;
  };
  std::map<std::string, ExpectedComponent> expected_components_;

  // Once we caught the component, we hold on into the controller to be able
  // to detach/kill it correctly.
  std::map<uint64_t, fuchsia::sys::ComponentControllerPtr> running_components_;

  std::map<debug_ipc::ExceptionType, debug_ipc::ExceptionStrategy> exception_strategies_;

  AgentConfiguration configuration_;

  fxl::WeakPtrFactory<DebugAgent> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebugAgent);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_AGENT_H_
