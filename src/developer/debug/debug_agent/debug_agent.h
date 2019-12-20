// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_AGENT_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_AGENT_H_

#include <zircon/types.h>

#include <map>
#include <memory>

#include "lib/sys/cpp/service_directory.h"
#include "src/developer/debug/debug_agent/agent_configuration.h"
#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/component_launcher.h"
#include "src/developer/debug/debug_agent/debugged_job.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/limbo_provider.h"
#include "src/developer/debug/debug_agent/object_provider.h"
#include "src/developer/debug/debug_agent/remote_api.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_agent {

// Set of dependencies the debug agent needs.
struct SystemProviders {
  // Creates the set of providers that represent the actual system. This is what you want to use for
  // the real debugger.
  static SystemProviders CreateDefaults(std::shared_ptr<sys::ServiceDirectory> services);

  std::shared_ptr<arch::ArchProvider> arch_provider;
  std::shared_ptr<LimboProvider> limbo_provider;
  std::shared_ptr<ObjectProvider> object_provider;
};

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
  //
  // |object_provider| provides a view into the Zircon process tree.
  // Can be overriden for test purposes.
  explicit DebugAgent(std::shared_ptr<sys::ServiceDirectory> services, SystemProviders providers);
  ~DebugAgent();

  fxl::WeakPtr<DebugAgent> GetWeakPtr();

  // Connects the debug agent to a stream buffer.
  // The buffer can be disconnected and the debug agent will remain intact until the moment a new
  // buffer is connected and messages start flowing through again.
  void Connect(debug_ipc::StreamBuffer*);
  void Disconnect();

  debug_ipc::StreamBuffer* stream();

  void RemoveDebuggedProcess(zx_koid_t process_koid);

  void RemoveDebuggedJob(zx_koid_t job_koid);

  void RemoveBreakpoint(uint32_t breakpoint_id);

  void OnProcessStart(const std::string& filter, zx::process) override;

  void InjectProcessForTest(std::unique_ptr<DebuggedProcess> process);

  bool should_quit() const { return configuration_.quit_on_exit; }

  DebuggedJob* GetDebuggedJob(zx_koid_t koid);
  DebuggedProcess* GetDebuggedProcess(zx_koid_t koid);
  DebuggedThread* GetDebuggedThread(zx_koid_t process_koid, zx_koid_t thread_koid);

 private:
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

  zx_status_t AddDebuggedJob(zx_koid_t job_koid, zx::job zx_job);
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

  void OnProcessesEnteredLimbo(std::vector<fuchsia::exception::ProcessExceptionMetadata>);

  // Members ---------------------------------------------------------------------------------------

  debug_ipc::StreamBuffer* stream_ = nullptr;

  std::shared_ptr<sys::ServiceDirectory> services_;

  std::map<zx_koid_t, std::unique_ptr<DebuggedJob>> jobs_;
  std::map<zx_koid_t, std::unique_ptr<DebuggedProcess>> procs_;

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

  AgentConfiguration configuration_;

  std::shared_ptr<arch::ArchProvider> arch_provider_;
  std::shared_ptr<LimboProvider> limbo_provider_;
  std::shared_ptr<ObjectProvider> object_provider_;

  fxl::WeakPtrFactory<DebugAgent> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebugAgent);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_AGENT_H_
