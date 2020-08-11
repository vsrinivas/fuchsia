// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/minidump.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/exception.h>

#include <third_party/crashpad/minidump/minidump_file_writer.h>
#include <third_party/crashpad/snapshot/fuchsia/process_snapshot_fuchsia.h>
#include <third_party/crashpad/util/fuchsia/scoped_task_suspend.h>

#include "src/lib/fsl/handles/object_info.h"

namespace forensics {
namespace exceptions {
namespace handler {

// GenerateVMOFromStringFile -----------------------------------------------------------------------

zx::vmo GenerateVMOFromStringFile(const crashpad::StringFile& string_file) {
  // We don't want to generate empty vmos.
  const std::string& data = string_file.string();
  if (data.empty())
    return {};

  zx::vmo vmo;
  if (zx_status_t status = zx::vmo::create(data.size(), 0, &vmo); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not create minidump VMO.";
    return {};
  }

  // Write the data into the vmo.
  if (zx_status_t status = vmo.write(data.data(), 0, data.size()); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not write data into VMO.";
    return {};
  }

  return vmo;
}

// GenerateMinidumpVMO -----------------------------------------------------------------------------

namespace {

zx::process GetProcess(const zx::exception& exception) {
  zx::process process;
  zx_status_t status = exception.get_process(&process);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not get process handle from exception.";
    return {};
  }
  return process;
}

zx::thread GetThread(const zx::exception& exception) {
  zx::thread thread;
  zx_status_t status = exception.get_thread(&thread);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not get thread handle from exception.";
    return {};
  }
  return thread;
}

}  // namespace

zx::vmo GenerateMinidump(zx::exception exception) {
  zx::process process = GetProcess(exception);
  if (!process.is_valid())
    return {};

  zx::thread thread = GetThread(exception);
  if (!thread.is_valid())
    return {};

  zx_koid_t thread_koid = fsl::GetKoid(thread.get());
  if (thread_koid == 0)
    return {};

  // Will unsuspend the process upon exiting the block.
  crashpad::ScopedTaskSuspend suspend(process);

  const std::string process_name = fsl::GetObjectName(process.get());
  zx_exception_report_t report;
  zx_status_t status =
      thread.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report), nullptr, nullptr);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Process " << process_name
                            << ": Could not obtain ZX_INFO_THREAD_EXCEPTION_REPORT.";
    return {};
  }

  // Create a process snapshot form the process and the exception thread.
  crashpad::ProcessSnapshotFuchsia process_snapshot;
  if (!process_snapshot.Initialize(process) ||
      !process_snapshot.InitializeException(thread_koid, report)) {
    FX_LOGS(ERROR) << "Process " << process_name << ": Could not create process snapshot.";
    return {};
  }

  crashpad::MinidumpFileWriter minidump;
  minidump.InitializeFromSnapshot(&process_snapshot);

  // Represents an in-memory backed file writer interface.
  crashpad::StringFile string_file;
  if (!minidump.WriteEverything(&string_file)) {
    FX_LOGS(ERROR) << "Process " << process_name << ": Failed to generate minidump.";
    return {};
  }

  zx::vmo vmo = GenerateVMOFromStringFile(string_file);
  if (!vmo.is_valid()) {
    FX_LOGS(ERROR) << "Process " << process_name << ": Could not generate vmo from minidump.";
    return {};
  }

  return vmo;
}

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics
