// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/insntrace/control.h"

#include <fcntl.h>
#include <fuchsia/hardware/cpu/insntrace/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/device/cpu-trace/intel-pt.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <cinttypes>
#include <iostream>

#include "garnet/bin/insntrace/config.h"
#include "garnet/bin/insntrace/ktrace_controller.h"
#include "garnet/bin/insntrace/utils.h"
#include "garnet/lib/debugger_utils/util.h"
#include "garnet/lib/debugger_utils/x86_cpuid.h"
#include "garnet/lib/debugger_utils/x86_pt.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace insntrace {

using Allocation = ::fuchsia::hardware::cpu::insntrace::Allocation;
using BufferConfig = ::fuchsia::hardware::cpu::insntrace::BufferConfig;
using BufferState = ::fuchsia::hardware::cpu::insntrace::BufferState;
using ControllerSyncPtr = ::fuchsia::hardware::cpu::insntrace::ControllerSyncPtr;

// This isn't emitted by the fidl compiler.
using BufferDescriptor = uint32_t;

static constexpr char ipt_device_path[] = "/dev/sys/cpu-trace/insntrace";

static constexpr char buffer_output_path_suffix[] = "pt";
static constexpr char ktrace_output_path_suffix[] = "ktrace";
static constexpr char cpuid_output_path_suffix[] = "cpuid";
static constexpr char pt_list_output_path_suffix[] = "ptlist";

static constexpr uint32_t kKtraceGroupMask = KTRACE_GRP_ARCH | KTRACE_GRP_TASKS;

static ControllerSyncPtr OpenDevice() {
  ControllerSyncPtr controller_ptr;
  zx_status_t status =
      fdio_service_connect(ipt_device_path, controller_ptr.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error connecting to " << ipt_device_path << ": " << status;
    return ControllerSyncPtr();
  }
  return controller_ptr;
}

bool AllocTrace(const IptConfig& config) {
  FX_LOGS(INFO) << "AllocTrace called";

  ControllerSyncPtr ipt{OpenDevice()};
  if (!ipt) {
    return false;
  }

  Allocation allocation;
  allocation.mode = config.mode;
  allocation.num_traces = (config.mode == Mode::CPU ? config.num_cpus : config.max_threads);
  FX_VLOGS(2) << fxl::StringPrintf("mode=%u, num_traces=0x%x",
                                   static_cast<unsigned>(allocation.mode), allocation.num_traces);

  ::fuchsia::hardware::cpu::insntrace::Controller_Initialize_Result result;
  zx_status_t status = ipt->Initialize(allocation, &result);
  if (status != ZX_OK || result.is_err()) {
    LogFidlFailure("Initialize", status, result.err());
    return false;
  }

  return true;
}

static void InitIptBufferConfig(BufferConfig* ipt_config, const IptConfig& config) {
  memset(ipt_config, 0, sizeof(*ipt_config));
  ipt_config->num_chunks = config.num_chunks;
  ipt_config->chunk_order = config.chunk_order;
  ipt_config->is_circular = config.is_circular;
  ipt_config->ctl = config.CtlMsr();
  ipt_config->address_space_match = config.cr3_match;
  ipt_config->address_range_0.start = config.AddrBegin(0);
  ipt_config->address_range_0.end = config.AddrEnd(0);
  ipt_config->address_range_1.start = config.AddrBegin(1);
  ipt_config->address_range_1.end = config.AddrEnd(1);
}

bool InitTrace(const IptConfig& config) {
  FX_LOGS(INFO) << "InitTrace called";
  FX_DCHECK(config.mode == Mode::CPU);

  ControllerSyncPtr ipt{OpenDevice()};
  if (!ipt) {
    return false;
  }

  for (uint32_t cpu = 0; cpu < config.num_cpus; ++cpu) {
    BufferConfig ipt_config;
    InitIptBufferConfig(&ipt_config, config);

    ::fuchsia::hardware::cpu::insntrace::Controller_AllocateBuffer_Result result;
    zx_status_t status = ipt->AllocateBuffer(ipt_config, &result);
    if (status != ZX_OK || result.is_err()) {
      LogFidlFailure("AllocateBuffer", status, result.err());
      return false;
    }
    // Buffers are automagically assigned to cpus, descriptor == cpu#,
    // so we can just ignore descriptor here.
  }

  return true;
}

// This must be called before a process is started so we emit a ktrace
// process start record for it.

bool InitProcessTrace(const IptConfig& config) {
  FX_LOGS(INFO) << "InitProcessTrace called";

  fuchsia::tracing::kernel::ControllerSyncPtr ktrace;
  if (!OpenKtraceChannel(&ktrace)) {
    return false;
  }

  // If tracing cpus we may want all the records for processes that were
  // started during boot, so don't reset ktrace here. If tracing threads it
  // doesn't much matter other than hopefully the necessary records don't get
  // over run, which is handled below by only enabling the collection groups
  // we need. So for now leave existing records alone.
  // A better solution would be to collect the data we need at the time we
  // need it.
#if 0  // TODO(dje)
  if (config.mode == Mode::THREAD) {
    RequestKtraceStop(ktrace);
    RequestKtraceRewind(ktrace);
  }
#endif

  // We definitely need ktrace turned on in order to get cr3->pid mappings,
  // which we need to map trace cr3 values to ld.so mappings, which we need in
  // order to be able to find the ELFs, which are required by the decoder.
  // So this isn't a nice-to-have, we need it. It's possible ktrace is
  // currently off, so ensure it's turned on.
  // For now just include arch info in the ktrace - we need it, and we don't
  // want to risk the ktrace buffer filling without it.
  // Also include task info to get process exit records - we need to know when
  // a cr3 value becomes invalid. Hopefully this won't cause the buffer to
  // overrun. It it does we could consider having special ktrace records just
  // for this, but that's a last resort kind of thing.
  if (RequestKtraceStart(ktrace, kKtraceGroupMask)) {
    return true;
  }

  // TODO(dje): Resume original ktracing? Need ability to get old value.
  RequestKtraceStop(ktrace);
  return false;
}

bool StartTrace(const IptConfig& config) {
  FX_LOGS(INFO) << "StartTrace called";
  FX_DCHECK(config.mode == Mode::CPU);

  ControllerSyncPtr ipt{OpenDevice()};
  if (!ipt) {
    return false;
  }

  zx_status_t status = ipt->Start();
  if (status != ZX_OK) {
    LogFidlFailure("Start", status);
    return false;
  }

  return true;
}

void StopTrace(const IptConfig& config) {
  FX_LOGS(INFO) << "StopTrace called";
  FX_DCHECK(config.mode == Mode::CPU);

  ControllerSyncPtr ipt{OpenDevice()};
  if (!ipt) {
    return;
  }

  [[maybe_unused]] zx_status_t status = ipt->Stop();
  FX_DCHECK(status == ZX_OK);
}

void StopSidebandDataCollection(const IptConfig& config) {
  FX_LOGS(INFO) << "StopSidebandDataCollection called";

  fuchsia::tracing::kernel::ControllerSyncPtr ktrace;
  if (!OpenKtraceChannel(&ktrace)) {
    return;
  }

  // Avoid having the records we need overrun by the time we collect them by
  // stopping ktrace here. It will get turned back on by "reset".
  RequestKtraceStop(ktrace);
}

static std::string GetCpuPtFileName(const std::string& output_path_prefix, uint64_t id) {
  const char* name_prefix = "cpu";
  return fxl::StringPrintf("%s.%s%" PRIu64 ".%s", output_path_prefix.c_str(), name_prefix, id,
                           buffer_output_path_suffix);
}

static std::string GetThreadPtFileName(const std::string& output_path_prefix, uint64_t id) {
  const char* name_prefix = "thr";
  return fxl::StringPrintf("%s.%s%" PRIu64 ".%s", output_path_prefix.c_str(), name_prefix, id,
                           buffer_output_path_suffix);
}

// Write the contents of buffer |descriptor| to a file.
// The file's name is $output_path_prefix.$name_prefix$id.pt.

static zx_status_t WriteBufferData(const IptConfig& config, const ControllerSyncPtr& ipt,
                                   uint32_t descriptor, uint64_t id) {
  std::string output_path;
  if (config.mode == Mode::CPU)
    output_path = GetCpuPtFileName(config.output_path_prefix, id);
  else
    output_path = GetThreadPtFileName(config.output_path_prefix, id);
  const char* c_path = output_path.c_str();

  // Refetch the buffer config as we can be invoked in a separate process,
  // after tracing has started, and shouldn't rely on what the user thinks
  // the config is.
  std::unique_ptr<BufferConfig> buffer_config;
  zx_status_t status = ipt->GetBufferConfig(descriptor, &buffer_config);
  if (status != ZX_OK) {
    LogFidlFailure("GetBufferConfig", status);
    return status;
  }
  if (!buffer_config) {
    FX_LOGS(ERROR) << "Failed getting buffer config";
    return ZX_ERR_INTERNAL;
  }

  std::unique_ptr<BufferState> buffer_state;
  status = ipt->GetBufferState(descriptor, &buffer_state);
  if (status != ZX_OK) {
    LogFidlFailure("GetBufferState", status);
    return status;
  }
  if (!buffer_state) {
    FX_LOGS(ERROR) << "Failed getting buffer state";
    return ZX_ERR_INTERNAL;
  }

  fxl::UniqueFD fd(open(c_path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR));
  if (!fd.is_valid()) {
    FX_LOGS(ERROR) << fxl::StringPrintf("Failed writing file: %s", c_path) << ", "
                   << debugger_utils::ErrnoString(errno);
    return ZX_ERR_BAD_PATH;
  }

  // TODO(dje): Fetch from vmo?
  size_t chunk_size = (1 << buffer_config->chunk_order) * PAGE_SIZE;
  uint32_t num_chunks = buffer_config->num_chunks;

  // If using a circular buffer there's (currently) no way to know if
  // tracing wrapped, so for now we just punt and always dump the entire
  // buffer. It's highly likely it wrapped anyway.
  size_t bytes_left;
  if (buffer_config->is_circular)
    bytes_left = num_chunks * chunk_size;
  else
    bytes_left = buffer_state->capture_end;

  FX_LOGS(INFO) << fxl::StringPrintf("Writing %zu bytes to %s", bytes_left, c_path);

  char buf[4096];

  for (uint32_t i = 0; i < num_chunks && bytes_left > 0; ++i) {
    zx::vmo vmo;
    status = ipt->GetChunkHandle(descriptor, i, &vmo);
    if (status != ZX_OK) {
      LogFidlFailure("GetChunkHandle", status);
      FX_LOGS(ERROR) << "Buffer " << descriptor << ", chunk " << i;
      goto Fail;
    }

    size_t buffer_remaining = chunk_size;
    size_t offset = 0;
    while (buffer_remaining && bytes_left) {
      size_t to_write = sizeof(buf);
      if (to_write > buffer_remaining)
        to_write = buffer_remaining;
      if (to_write > bytes_left)
        to_write = bytes_left;
      // TODO(dje): Mapping into process and reading directly from that
      // left for another day.
      status = vmo.read(buf, offset, to_write);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << fxl::StringPrintf("zx_vmo_read: buffer %u, buffer %u, offset %zu: ",
                                            descriptor, i, offset)
                       << debugger_utils::ZxErrorString(status);
        goto Fail;
      }
      if (write(fd.get(), buf, to_write) != (ssize_t)to_write) {
        FX_LOGS(ERROR) << fxl::StringPrintf("Short write, file: %s\n", c_path);
        status = ZX_ERR_IO;
        goto Fail;
      }
      offset += to_write;
      buffer_remaining -= to_write;
      bytes_left -= to_write;
    }
  }

  assert(bytes_left == 0);
  status = ZX_OK;
  // fallthrough

Fail:
  // We don't delete the file on failure on purpose, it is kept for
  // debugging purposes.
  return status;
}

// Write all output files.
// This assumes tracing has already been stopped.

void DumpTrace(const IptConfig& config) {
  FX_LOGS(INFO) << "DumpTrace called";
  FX_DCHECK(config.mode == Mode::CPU);

  ControllerSyncPtr ipt{OpenDevice()};
  if (!ipt) {
    return;
  }

  for (uint32_t cpu = 0; cpu < config.num_cpus; ++cpu) {
    // Buffer descriptors for cpus is the cpu number.
    auto status = WriteBufferData(config, ipt, cpu, cpu);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << fxl::StringPrintf("Dump perf of cpu %u: ", cpu)
                     << debugger_utils::ZxErrorString(status);
      // Keep trying to dump other cpu's data.
    }
  }
}

void DumpSidebandData(const IptConfig& config) {
  FX_LOGS(INFO) << "DumpSidebandData called";

  DumpKtraceBuffer(config.output_path_prefix.c_str(), ktrace_output_path_suffix);

  // TODO(dje): UniqueFILE?
  {
    std::string cpuid_output_path =
        fxl::StringPrintf("%s.%s", config.output_path_prefix.c_str(), cpuid_output_path_suffix);
    const char* cpuid_c_path = cpuid_output_path.c_str();

    FILE* f = fopen(cpuid_c_path, "w");
    if (f != nullptr) {
      debugger_utils::x86_feature_debug(f);
      // Also put the mtc_freq value in the cpuid file, it's as good a place
      // for it as any. See intel-pt.h:pt_config.
      // Alternatively this could be added to the ktrace record.
      // TODO(dje): Put constants in zircon/device/intel-pt.h.
      unsigned mtc_freq = config.mtc_freq;
      fprintf(f, "mtc_freq: %u\n", mtc_freq);
      // TODO(dje): verify writes succeed
      fclose(f);
    } else {
      FX_LOGS(ERROR) << "unable to write PT config to " << cpuid_c_path;
    }
  }

  // TODO(dje): UniqueFILE?
  // TODO(dje): Handle Mode::THREAD
  if (config.mode == Mode::CPU) {
    std::string pt_list_output_path =
        fxl::StringPrintf("%s.%s", config.output_path_prefix.c_str(), pt_list_output_path_suffix);
    const char* pt_list_c_path = pt_list_output_path.c_str();

    FILE* f = fopen(pt_list_c_path, "w");
    if (f != nullptr) {
      for (uint32_t cpu = 0; cpu < config.num_cpus; ++cpu) {
        std::string pt_file = GetCpuPtFileName(config.output_path_prefix, cpu);
        fprintf(f, "%u %s\n", cpu, pt_file.c_str());
      }
      // TODO(dje): verify writes succeed
      fclose(f);
    } else {
      FX_LOGS(ERROR) << "unable to write PT list to " << pt_list_c_path;
    }
  }
}

void ResetTrace(const IptConfig& config) {
  FX_LOGS(INFO) << "ResetTrace called";
  FX_DCHECK(config.mode == Mode::CPU);

  // TODO(dje): Nothing to do currently. There use to be. So keep this
  // function around for a bit.
}

// Free all resources associated with the true.
// This means restoring ktrace to its original state.
// This assumes tracing has already been stopped.

void FreeTrace(const IptConfig& config) {
  FX_LOGS(INFO) << "FreeTrace called";

  ControllerSyncPtr ipt{OpenDevice()};
  if (!ipt) {
    return;
  }

  ::fuchsia::hardware::cpu::insntrace::Controller_Terminate_Result result;
  zx_status_t status = ipt->Terminate(&result);
  if (status != ZX_OK || result.is_err()) {
    LogFidlFailure("Terminate", status, result.err());
  }

  // TODO(dje): Resume original ktracing? Need ability to get old value.
  // For now set the values to what we need: A later run might still need
  // the boot time records.

  fuchsia::tracing::kernel::ControllerSyncPtr ktrace;
  if (!OpenKtraceChannel(&ktrace)) {
    return;
  }

  RequestKtraceStop(ktrace);
#if 0  // TODO(dje): See rewind comments in InitProcessTrace.
  RequestKtraceRewind(ktrace);
#endif
  RequestKtraceStart(ktrace, kKtraceGroupMask);
}

}  // namespace insntrace
