// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): wip wip wip

#include "control.h"

#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cinttypes>

#include <iostream>

#include <zircon/device/cpu-trace/intel-pt.h>
#include <zircon/device/ktrace.h>
#include <zircon/ktrace.h>
#include <zircon/syscalls.h>

#include <lib/fdio/util.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/util.h"
#include "garnet/lib/debugger_utils/x86_pt.h"

#include "garnet/lib/inferior_control/arch.h"
#include "garnet/lib/inferior_control/arch_x86.h"

#include "server.h"

namespace debugserver {

static constexpr char ipt_device_path[] = "/dev/sys/cpu-trace/cpu-trace";
static constexpr char ktrace_device_path[] = "/dev/misc/ktrace";

static constexpr char buffer_output_path_suffix[] = "pt";
static constexpr char ktrace_output_path_suffix[] = "ktrace";
static constexpr char cpuid_output_path_suffix[] = "cpuid";
static constexpr char pt_list_output_path_suffix[] = "ptlist";

static constexpr uint32_t kKtraceGroupMask =
  KTRACE_GRP_ARCH | KTRACE_GRP_TASKS;

// Temporary bridge to walk v2 changes through zircon pinning.

#if IPT_API_VERSION >= 2

ssize_t ioctl_ipt_set_mode(int fd, const uint32_t* mode) {
  return ZX_ERR_NOT_SUPPORTED;
}

ssize_t ioctl_ipt_cpu_mode_alloc(int fd) {
  return ZX_ERR_NOT_SUPPORTED;
}

ssize_t ioctl_ipt_cpu_mode_start(int fd) {
  return ZX_ERR_NOT_SUPPORTED;
}

ssize_t ioctl_ipt_cpu_mode_stop(int fd) {
  return ZX_ERR_NOT_SUPPORTED;
}

ssize_t ioctl_ipt_cpu_mode_free(int fd) {
  return ZX_ERR_NOT_SUPPORTED;
}

#endif

static bool OpenDevices(fxl::UniqueFD* out_ipt_fd,
                        fxl::UniqueFD* out_ktrace_fd,
                        zx::handle* out_ktrace_handle) {
  int ipt_fd = -1;
  int ktrace_fd = -1;
  zx_handle_t ktrace_handle = ZX_HANDLE_INVALID;

  if (out_ipt_fd) {
    ipt_fd = open(ipt_device_path, O_RDONLY);
    if (ipt_fd < 0) {
      FXL_LOG(ERROR) << "unable to open " << ipt_device_path
                     << ": " << ErrnoString(errno);
      return false;
    }
  }

  if (out_ktrace_fd || out_ktrace_handle) {
    ktrace_fd = open(ktrace_device_path, O_RDONLY);
    if (ktrace_fd < 0) {
      FXL_LOG(ERROR) << "open ktrace"
                     << ", " << ErrnoString(errno);
      close(ipt_fd);
      return false;
    }
  }

  if (out_ktrace_handle) {
    ssize_t ssize = ioctl_ktrace_get_handle(ktrace_fd, &ktrace_handle);
    if (ssize != sizeof(ktrace_handle)) {
      FXL_LOG(ERROR) << "get ktrace handle" << ", " << ErrnoString(errno);
      close(ipt_fd);
      close(ktrace_fd);
      return false;
    }
  }

  if (out_ipt_fd) {
    out_ipt_fd->reset(ipt_fd);
  }
  if (out_ktrace_fd) {
    out_ktrace_fd->reset(ktrace_fd);
  } else if (ktrace_fd != -1) {
    // Only needed to get ktrace handle.
    close(ktrace_fd);
  }
  if (out_ktrace_handle) {
    out_ktrace_handle->reset(ktrace_handle);
  }

  return true;
}

bool AllocTrace(const IptConfig& config) {
  FXL_LOG(INFO) << "AllocTrace called";

  fxl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return false;

  ioctl_ipt_trace_config_t trace_config;
  trace_config.mode = config.mode;
  ssize_t ssize = ioctl_ipt_alloc_trace(ipt_fd.get(), &trace_config);
  if (ssize < 0) {
    FXL_LOG(ERROR) << "set perf mode: " << ZxErrorString(ssize);
    goto Fail;
  }

  return true;

 Fail:
  return false;
}

static void InitIptBufferConfig(ioctl_ipt_buffer_config_t* ipt_config,
                                const IptConfig& config) {
  memset(ipt_config, 0, sizeof(*ipt_config));
#if IPT_API_VERSION == 0
  ipt_config->num_buffers = config.num_chunks;
  ipt_config->buffer_order = config.chunk_order;
#else
  ipt_config->num_chunks = config.num_chunks;
  ipt_config->chunk_order = config.chunk_order;
#endif
  ipt_config->is_circular = config.is_circular;
  ipt_config->ctl = config.CtlMsr();
  ipt_config->cr3_match = config.cr3_match;
  ipt_config->addr_ranges[0].a = config.AddrBegin(0);
  ipt_config->addr_ranges[0].b = config.AddrEnd(0);
  ipt_config->addr_ranges[1].a = config.AddrBegin(1);
  ipt_config->addr_ranges[1].b = config.AddrEnd(1);
}

bool InitCpuPerf(const IptConfig& config) {
  FXL_LOG(INFO) << "InitCpuPerf called";
  FXL_DCHECK(config.mode == IPT_MODE_CPUS);

  fxl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return false;

  for (uint32_t cpu = 0; cpu < config.num_cpus; ++cpu) {
    ioctl_ipt_buffer_config_t ipt_config;
    InitIptBufferConfig(&ipt_config, config);

    uint32_t descriptor;
    auto ssize = ioctl_ipt_alloc_buffer(ipt_fd.get(), &ipt_config, &descriptor);
    if (ssize < 0) {
      FXL_LOG(ERROR) << "init cpu perf: " << ZxErrorString(ssize);
      goto Fail;
    }
    // Buffers are automagically assigned to cpus, descriptor == cpu#,
    // so we can just ignore descriptor here.
  }

  return true;

 Fail:
  return false;
}

bool InitThreadPerf(Thread* thread, const IptConfig& config) {
  FXL_LOG(INFO) << "InitThreadPerf called";
  FXL_DCHECK(config.mode == IPT_MODE_THREADS);

  fxl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return false;

  ioctl_ipt_buffer_config_t ipt_config;
  InitIptBufferConfig(&ipt_config, config);

  uint32_t descriptor;
  ssize_t ssize = ioctl_ipt_alloc_buffer(ipt_fd.get(), &ipt_config,
                                         &descriptor);
  if (ssize < 0) {
    FXL_LOG(ERROR) << "init thread perf: " << ZxErrorString(ssize);
    goto Fail;
  }

  thread->set_ipt_buffer(descriptor);
  return true;

 Fail:
  return false;
}

// This must be called before a process is started so we emit a ktrace
// process start record for it.

bool InitPerfPreProcess(const IptConfig& config) {
  FXL_LOG(INFO) << "InitPerfPreProcess called";

  zx::handle ktrace_handle;
  zx_status_t status;

  if (!OpenDevices(nullptr, nullptr, &ktrace_handle))
    return false;

  // If tracing cpus we may want all the records for processes that were
  // started during boot, so don't reset ktrace here. If tracing threads it
  // doesn't much matter other than hopefully the necessary records don't get
  // over run, which is handled below by only enabling the collection groups
  // we need. So for now leave existing records alone.
  // We also need to make the distinction of ktrace records for processes
  // started during boot (they can appear a fair bit in traces), and random
  // processes that were started later that have nothing to do with what we
  // want to collect. IWBN to capture the ktrace records from boot and save
  // them away. Then it'd make more sense to rewind here, though even then
  // the user may have started something important before we get run. Another
  // thought is to provide an action to control ktrace specifically.
#if 0 // TODO(dje)
  if (config.mode == IPT_MODE_THREADS) {
  status = zx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_STOP, 0,
                             nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "ktrace stop: " << ZxErrorString(status);
    goto Fail;
  }
  status = zx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_REWIND, 0,
                             nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "ktrace rewind: " << ZxErrorString(status);
    goto Fail;
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
  status = zx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_START,
                             kKtraceGroupMask, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "ktrace start: " << ZxErrorString(status);
    goto Fail;
  }

  return true;

 Fail:
  // TODO(dje): Resume original ktracing? Need ability to get old value.
  // For now set the values to what we need: A later run might still need
  // the boot time records.
  zx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_STOP, 0, nullptr);
  zx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_START,
                    kKtraceGroupMask, nullptr);

  return false;
}

bool StartCpuPerf(const IptConfig& config) {
  FXL_LOG(INFO) << "StartCpuPerf called";
  FXL_DCHECK(config.mode == IPT_MODE_CPUS);

  fxl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return false;

  ssize_t ssize = ioctl_ipt_start(ipt_fd.get());
  if (ssize < 0) {
    FXL_LOG(ERROR) << "start cpu perf: " << ZxErrorString(ssize);
    return false;
  }

  return true;
}

bool StartThreadPerf(Thread* thread, const IptConfig& config) {
  FXL_LOG(INFO) << "StartThreadPerf called";
  FXL_DCHECK(config.mode == IPT_MODE_THREADS);

  if (thread->ipt_buffer() < 0) {
    FXL_LOG(INFO) << fxl::StringPrintf("Thread %" PRIu64 " has no IPT buffer",
                                       thread->id());
    // TODO(dje): For now. This isn't an error in the normal sense.
    return true;
  }

  fxl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return false;

  ioctl_ipt_assign_buffer_thread_t assign;
  zx_status_t status;
  ssize_t ssize;

  status = zx_handle_duplicate(thread->handle(), ZX_RIGHT_SAME_RIGHTS,
                               &assign.thread);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "duplicating thread handle: "
                   << ZxErrorString(status);
    goto Fail;
  }
  assign.descriptor = thread->ipt_buffer();
  ssize = ioctl_ipt_assign_buffer_thread(ipt_fd.get(), &assign);
  if (ssize < 0) {
    FXL_LOG(ERROR) << "assigning ipt buffer to thread: "
                   << ZxErrorString(ssize);
    goto Fail;
  }

  return true;

 Fail:
  return false;
}

void StopCpuPerf(const IptConfig& config) {
  FXL_LOG(INFO) << "StopCpuPerf called";
  FXL_DCHECK(config.mode == IPT_MODE_CPUS);

  fxl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  ssize_t ssize = ioctl_ipt_stop(ipt_fd.get());
  if (ssize < 0) {
    // TODO(dje): This is really bad, this shouldn't fail.
    FXL_LOG(ERROR) << "stop cpu perf: " << ZxErrorString(ssize);
  }
}

void StopThreadPerf(Thread* thread, const IptConfig& config) {
  FXL_LOG(INFO) << "StopThreadPerf called";
  FXL_DCHECK(config.mode == IPT_MODE_THREADS);

  if (thread->ipt_buffer() < 0) {
    FXL_LOG(INFO) << fxl::StringPrintf("Thread %" PRIu64 " has no IPT buffer",
                                       thread->id());
    return;
  }

  fxl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  ioctl_ipt_assign_buffer_thread_t assign;
  zx_handle_t status;
  ssize_t ssize;

  status = zx_handle_duplicate(thread->handle(), ZX_RIGHT_SAME_RIGHTS,
                               &assign.thread);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "duplicating thread handle: "
                   << ZxErrorString(status);
    goto Fail;
  }
  assign.descriptor = thread->ipt_buffer();
  ssize = ioctl_ipt_release_buffer_thread(ipt_fd.get(), &assign);
  if (ssize < 0) {
    FXL_LOG(ERROR) << "releasing ipt buffer from thread: "
                   << ZxErrorString(ssize);
    goto Fail;
  }

 Fail:
  ; // nothing to do
}

void StopPerf(const IptConfig& config) {
  FXL_LOG(INFO) << "StopPerf called";

  zx::handle ktrace_handle;
  if (!OpenDevices(nullptr, nullptr, &ktrace_handle))
    return;

  // Avoid having the records we need overrun by the time we collect them by
  // stopping ktrace here. It will get turned back on by "reset".
  zx_status_t status =
    zx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_STOP, 0, nullptr);
  if (status != ZX_OK) {
    // TODO(dje): This shouldn't fail either, should it?
    FXL_LOG(ERROR) << "stop ktrace: " << ZxErrorString(status);
  }
}

static std::string GetCpuPtFileName(const std::string& output_path_prefix,
                                    uint64_t id) {
  const char* name_prefix = "cpu";
  return fxl::StringPrintf("%s.%s%" PRIu64 ".%s", output_path_prefix.c_str(),
                           name_prefix, id, buffer_output_path_suffix);
}

static std::string GetThreadPtFileName(const std::string& output_path_prefix,
                                       uint64_t id) {
  const char* name_prefix = "thr";
  return fxl::StringPrintf("%s.%s%" PRIu64 ".%s", output_path_prefix.c_str(),
                           name_prefix, id, buffer_output_path_suffix);
}

// Write the contents of buffer |descriptor| to a file.
// The file's name is $output_path_prefix.$name_prefix$id.pt.

static zx_status_t WriteBufferData(const IptConfig& config,
                                   const fxl::UniqueFD& ipt_fd,
                                   uint32_t descriptor,
                                   uint64_t id) {
  std::string output_path;
  if (config.mode == IPT_MODE_CPUS)
    output_path = GetCpuPtFileName(config.output_path_prefix, id);
  else
    output_path = GetThreadPtFileName(config.output_path_prefix, id);
  const char* c_path = output_path.c_str();

  zx_status_t status = ZX_OK;

  // Refetch the buffer config as we can be invoked in a separate process,
  // after tracing has started, and shouldn't rely on what the user thinks
  // the config is.
  ioctl_ipt_buffer_config_t buffer_config;
  ssize_t ssize = ioctl_ipt_get_buffer_config(ipt_fd.get(), &descriptor,
                                              &buffer_config);
  if (ssize < 0) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
                          "ioctl_ipt_get_buffer_config: buffer %u: ",
                          descriptor)
                   << ZxErrorString(ssize);
    return ssize;
  }

  ioctl_ipt_buffer_info_t info;
  ssize = ioctl_ipt_get_buffer_info(ipt_fd.get(), &descriptor, &info);
  if (ssize < 0) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
                          "ioctl_ipt_get_buffer_info: buffer %u: ", descriptor)
                   << ZxErrorString(ssize);
    return ssize;
  }

  fxl::UniqueFD fd(open(c_path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR));
  if (!fd.is_valid()) {
    FXL_LOG(ERROR) << fxl::StringPrintf("unable to write file: %s", c_path)
                   << ", " << ErrnoString(errno);
    return ZX_ERR_BAD_PATH;
  }

  // TODO(dje): Fetch from vmo?
#if IPT_API_VERSION == 0
  size_t chunk_size = (1 << buffer_config.buffer_order) * PAGE_SIZE;
  uint32_t num_chunks = buffer_config.num_buffers;
#else
  size_t chunk_size = (1 << buffer_config.chunk_order) * PAGE_SIZE;
  uint32_t num_chunks = buffer_config.num_chunks;
#endif

  // If using a circular buffer there's (currently) no way to know if
  // tracing wrapped, so for now we just punt and always dump the entire
  // buffer. It's highly likely it wrapped anyway.
  size_t bytes_left;
  if (buffer_config.is_circular)
    bytes_left = num_chunks * chunk_size;
  else
    bytes_left = info.capture_end;

  FXL_LOG(INFO) << fxl::StringPrintf("Writing %zu bytes to %s",
                                     bytes_left, c_path);

  char buf[4096];

  for (uint32_t i = 0; i < num_chunks && bytes_left > 0; ++i) {
#if IPT_API_VERSION == 0
    ioctl_ipt_buffer_handle_req_t handle_rqst;
#else
    ioctl_ipt_chunk_handle_req_t handle_rqst;
#endif
    handle_rqst.descriptor = descriptor;
#if IPT_API_VERSION == 0
    handle_rqst.buffer_num = i;
#else
    handle_rqst.chunk_num = i;
#endif
    zx_handle_t vmo_handle;
#if IPT_API_VERSION == 0
    ssize = ioctl_ipt_get_buffer_handle(ipt_fd.get(), &handle_rqst,
                                        &vmo_handle);
#else
    ssize = ioctl_ipt_get_chunk_handle(ipt_fd.get(), &handle_rqst,
                                       &vmo_handle);
#endif
    if (ssize < 0) {
      FXL_LOG(ERROR)
          << fxl::StringPrintf(
                 "ioctl_ipt_get_buffer_handle: buffer %u, buffer %u: ",
                 descriptor, i)
          << ZxErrorString(ssize);
      goto Fail;
    }
    zx::vmo vmo(vmo_handle);

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
        FXL_LOG(ERROR) << fxl::StringPrintf(
                              "zx_vmo_read: buffer %u, buffer %u, offset %zu: ",
                              descriptor, i, offset)
                       << ZxErrorString(status);
        goto Fail;
      }
      if (write(fd.get(), buf, to_write) != (ssize_t) to_write) {
        FXL_LOG(ERROR) << fxl::StringPrintf("short write, file: %s\n", c_path);
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

void DumpCpuPerf(const IptConfig& config) {
  FXL_LOG(INFO) << "DumpCpuPerf called";
  FXL_DCHECK(config.mode == IPT_MODE_CPUS);

  fxl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  for (uint32_t cpu = 0; cpu < config.num_cpus; ++cpu) {
    // Buffer descriptors for cpus is the cpu number.
    auto status = WriteBufferData(config, ipt_fd, cpu, cpu);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << fxl::StringPrintf("dump perf of cpu %u: ", cpu)
                     << ZxErrorString(status);
      // Keep trying to dump other cpu's data.
    }
  }
}

// Write the buffer contents for |thread|.
// This assumes the thread is stopped.

void DumpThreadPerf(Thread* thread, const IptConfig& config) {
  FXL_LOG(INFO) << "DumpThreadPerf called";
  FXL_DCHECK(config.mode == IPT_MODE_THREADS);

  zx_koid_t id = thread->id();

  if (thread->ipt_buffer() < 0) {
    FXL_LOG(INFO) << fxl::StringPrintf("Thread %" PRIu64 " has no IPT buffer",
                                       id);
    return;
  }

  fxl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  auto status = WriteBufferData(config, ipt_fd, thread->ipt_buffer(), id);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << fxl::StringPrintf("dump perf of thread %" PRIu64 ": ", id)
                   << ZxErrorString(status);
  }
}

void DumpPerf(const IptConfig& config) {
  FXL_LOG(INFO) << "DumpPerf called";

  {
    fxl::UniqueFD ktrace_fd;
    if (!OpenDevices(nullptr, &ktrace_fd, nullptr))
      return;

    std::string ktrace_output_path =
      fxl::StringPrintf("%s.%s", config.output_path_prefix.c_str(),
                        ktrace_output_path_suffix);
    const char* ktrace_c_path = ktrace_output_path.c_str();

    fxl::UniqueFD dest_fd(open(ktrace_c_path, O_CREAT | O_TRUNC | O_RDWR,
                               S_IRUSR | S_IWUSR));
    if (dest_fd.is_valid()) {
      ssize_t count;
      char buf[1024];
      while ((count = read(ktrace_fd.get(), buf, sizeof(buf))) != 0) {
        if (write(dest_fd.get(), buf, count) != count) {
          FXL_LOG(ERROR) << "error writing " << ktrace_c_path;
        }
      }
    } else {
      FXL_LOG(ERROR) << fxl::StringPrintf("unable to create %s", ktrace_c_path)
                     << ", " << ErrnoString(errno);
    }
  }

  // TODO(dje): UniqueFILE?
  {
    std::string cpuid_output_path =
      fxl::StringPrintf("%s.%s", config.output_path_prefix.c_str(),
                        cpuid_output_path_suffix);
    const char* cpuid_c_path = cpuid_output_path.c_str();

    FILE* f = fopen(cpuid_c_path, "w");
    if (f != nullptr) {
      DumpArch(f);
      // Also put the mtc_freq value in the cpuid file, it's as good a place
      // for it as any. See intel-pt.h:pt_config.
      // Alternatively this could be added to the ktrace record.
      // TODO(dje): Put constants in zircon/device/intel-pt.h.
      unsigned mtc_freq = config.mtc_freq;
      fprintf(f, "mtc_freq: %u\n", mtc_freq);
      // TODO(dje): verify writes succeed
      fclose(f);
    } else {
      FXL_LOG(ERROR) << "unable to write PT config to " << cpuid_c_path;
    }
  }

  // TODO(dje): UniqueFILE?
  // TODO(dje): Handle IPT_MODE_THREADS
  if (config.mode == IPT_MODE_CPUS) {
    std::string pt_list_output_path =
      fxl::StringPrintf("%s.%s", config.output_path_prefix.c_str(),
                        pt_list_output_path_suffix);
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
      FXL_LOG(ERROR) << "unable to write PT list to " << pt_list_c_path;
    }
  }
}

void ResetCpuPerf(const IptConfig& config) {
  FXL_LOG(INFO) << "ResetCpuPerf called";
  FXL_DCHECK(config.mode == IPT_MODE_CPUS);

  // TODO(dje): Nothing to do currently. There use to be. So keep this
  // function around for a bit.
}

void ResetThreadPerf(Thread* thread, const IptConfig& config) {
  FXL_LOG(INFO) << "ResetThreadPerf called";
  FXL_DCHECK(config.mode == IPT_MODE_THREADS);

  if (thread->ipt_buffer() < 0) {
    FXL_LOG(INFO) << fxl::StringPrintf("Thread %" PRIu64 " has no IPT buffer",
                                       thread->id());
    return;
  }

  fxl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  uint32_t descriptor = thread->ipt_buffer();
  ssize_t ssize = ioctl_ipt_free_buffer(ipt_fd.get(), &descriptor);
  if (ssize < 0) {
    FXL_LOG(ERROR) << "freeing ipt buffer: " << ZxErrorString(ssize);
    goto Fail;
  }

 Fail:
  thread->set_ipt_buffer(-1);
}

// Free all resources associated with the true.
// This means restoring ktrace to its original state.
// This assumes tracing has already been stopped.

void FreeTrace(const IptConfig& config) {
  FXL_LOG(INFO) << "FreeTrace called";

  fxl::UniqueFD ipt_fd;
  zx::handle ktrace_handle;
  if (!OpenDevices(&ipt_fd, nullptr, &ktrace_handle))
    return;

  ssize_t ssize = ioctl_ipt_free_trace(ipt_fd.get());
  if (ssize < 0) {
    FXL_LOG(ERROR) << "ioctl_ipt_free_trace failed: " << ZxErrorString(ssize);
  }

  // TODO(dje): Resume original ktracing? Need ability to get old value.
  // For now set the values to what we need: A later run might still need
  // the boot time records.
  zx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_STOP, 0, nullptr);
#if 0 // TODO(dje): See rewind comments in InitPerfPreProcess.
  zx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_REWIND, 0, nullptr);
#endif
  zx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_START,
                    kKtraceGroupMask, nullptr);
}

} // debugserver namespace
