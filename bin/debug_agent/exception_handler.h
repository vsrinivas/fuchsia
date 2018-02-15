// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <threads.h>
#include <zx/port.h>
#include <zx/process.h>
#include <zx/socket.h>
#include <zx/thread.h>

// This exception handler class runs a background thread that blocks on
// exceptions from processes being debugged.
class ExceptionHandler {
 public:
  ExceptionHandler();
  ~ExceptionHandler();

  bool Start(zx::socket socket);

  // Attaches the exception handler to the given process. It must already have
  // been Start()ed.
  bool Attach(zx::process&& process);

 private:
  struct DebuggedProcess;

  // Implements the background thread.
  int DoThread();

  void OnSocketReadable();

  // Returns true if there are no more processes being debugged.
  bool OnProcessTerminated(const zx_port_packet_t& packet);

  // Handlers.
  void OnGeneralException(const zx_port_packet_t& packet,
                          const zx::thread& thread);
  void OnFatalPageFault(const zx_port_packet_t& packet,
                        const zx::thread& thread);
  void OnUndefinedInstruction(const zx_port_packet_t& packet,
                              const zx::thread& thread);
  void OnSoftwareBreakpoint(const zx_port_packet_t& packet,
                            const zx::thread& thread);
  void OnHardwareBreakpoint(const zx_port_packet_t& packet,
                            const zx::thread& thread);
  void OnUnalignedAccess(const zx_port_packet_t& packet,
                         const zx::thread& thread);
  void OnThreadStarting(const zx_port_packet_t& packet,
                        const zx::thread& thread);
  void OnThreadExiting(const zx_port_packet_t& packet,
                       const zx::thread& thread);
  void OnThreadPolicyError(const zx_port_packet_t& packet,
                           const zx::thread& thread);

  // Looks up the given Koid in the processes_ vector, returning it if found,
  // nullptr on not.
  const DebuggedProcess* ProcessForKoid(zx_koid_t koid);

  zx::socket socket_;  // Commands from client.

  thrd_t thread_;
  zx::port port_;

  // The list of all debugged processes. Protected by the Mutex. This uses
  // pointers so the
  // DebuggedProcess data is stable across mutations.
  fbl::Mutex mutex_;
  fbl::Vector<fbl::unique_ptr<DebuggedProcess>> processes_;
};
