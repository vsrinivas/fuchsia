// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): wip wip wip

#include "ipt-ctrl.h"

#include <cinttypes>
#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>

#include <magenta/device/intel-pt.h>
#include <magenta/device/ktrace.h>
#include <magenta/ktrace.h>
#include <magenta/syscalls.h>

#include <mx/handle.h>
#include <mx/vmo.h>
#include <mxio/util.h>

#include "debugger-utils/util.h"
#include "debugger-utils/x86-pt.h"

#include "inferior-control/arch.h"
#include "inferior-control/arch-x86.h"

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "ipt-server.h"

namespace debugserver {

static constexpr char ipt_device_path[] = "/dev/misc/intel-pt";
static constexpr char ktrace_device_path[] = "/dev/misc/ktrace";

static constexpr char pt_output_path_prefix[] = "/tmp/ptout";
static constexpr char ktrace_output_path[] = "/tmp/ptout.ktrace";

static constexpr char cpuid_output_path[] = "/tmp/ptout.cpuid";

static bool OpenDevices(ftl::UniqueFD* out_ipt_fd,
                        ftl::UniqueFD* out_ktrace_fd,
                        mx::handle* out_ktrace_handle) {
  int ipt_fd = -1;
  int ktrace_fd = -1;
  mx_handle_t ktrace_handle = MX_HANDLE_INVALID;

  if (out_ipt_fd) {
    ipt_fd = open(ipt_device_path, O_RDONLY);
    if (ipt_fd < 0) {
      util::LogErrorWithErrno("open intel-pt");
      return false;
    }
  }

  if (out_ktrace_fd || out_ktrace_handle) {
    ktrace_fd = open(ktrace_device_path, O_RDONLY);
    if (ktrace_fd < 0) {
      util::LogErrorWithErrno("open ktrace");
      close(ipt_fd);
      return false;
    }
  }

  if (out_ktrace_handle) {
    ssize_t ssize = ioctl_ktrace_get_handle(ktrace_fd, &ktrace_handle);
    if (ssize != sizeof(ktrace_handle)) {
      util::LogErrorWithErrno("get ktrace handle");
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

bool SetPerfMode(const IptConfig& config) {
  FTL_LOG(INFO) << "SetPerfMode called";

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return false;

  uint32_t mode = config.mode;
  ssize_t ssize = ioctl_ipt_set_mode(ipt_fd.get(), &mode);
  if (ssize < 0) {
    util::LogErrorWithMxStatus("set perf mode", ssize);
    goto Fail;
  }

  return true;

 Fail:
  return false;
}

bool InitCpuPerf(const IptConfig& config) {
  FTL_LOG(INFO) << "InitCpuPerf called";
  FTL_DCHECK(config.mode == IPT_MODE_CPUS);

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return false;

  ssize_t ssize;

  for (uint32_t cpu = 0; cpu < config.num_cpus; ++cpu) {
    ioctl_ipt_buffer_config_t ipt_config;
    uint32_t descriptor;
    memset(&ipt_config, 0, sizeof(ipt_config));
    ipt_config.num_buffers = config.num_buffers;
    ipt_config.buffer_order = config.buffer_order;
    ipt_config.is_circular = config.is_circular;
    ipt_config.ctl = config.ctl_config;
    ssize = ioctl_ipt_alloc_buffer(ipt_fd.get(), &ipt_config, &descriptor);
    if (ssize < 0) {
      util::LogErrorWithMxStatus("init cpu perf", ssize);
      goto Fail;
    }
    // Buffers are automagically assigned to cpus, descriptor == cpu#,
    // so we can just ignore descriptor here.
  }

  ssize = ioctl_ipt_cpu_mode_alloc(ipt_fd.get());
  if (ssize < 0) {
    util::LogErrorWithMxStatus("init perf", ssize);
    goto Fail;
  }

  return true;

 Fail:
  return false;
}

bool InitThreadPerf(Thread* thread, const IptConfig& config) {
  FTL_LOG(INFO) << "InitThreadPerf called";
  FTL_DCHECK(config.mode == IPT_MODE_THREADS);

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return false;

  ioctl_ipt_buffer_config_t ipt_config;
  uint32_t descriptor;
  memset(&ipt_config, 0, sizeof(ipt_config));
  ipt_config.num_buffers = config.num_buffers;
  ipt_config.buffer_order = config.buffer_order;
  ipt_config.is_circular = config.is_circular;
  ipt_config.ctl = config.ctl_config;
  ssize_t ssize = ioctl_ipt_alloc_buffer(ipt_fd.get(), &ipt_config,
                                         &descriptor);
  if (ssize < 0) {
    util::LogErrorWithMxStatus("init thread perf", ssize);
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
  FTL_LOG(INFO) << "InitPerfPreProcess called";

  mx::handle ktrace_handle;
  mx_status_t status;

  if (!OpenDevices(nullptr, nullptr, &ktrace_handle))
    return false;

  status = mx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_STOP, 0,
                             nullptr);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("ktrace stop", status);
    goto Fail;
  }
  status = mx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_REWIND, 0,
                             nullptr);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("ktrace rewind", status);
    goto Fail;
  }
  // For now just include arch info in the ktrace - we need it, and we don't
  // want to risk the ktrace buffer filling without it.
  status = mx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_START,
                             KTRACE_GRP_ARCH, nullptr);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("ktrace start", status);
    goto Fail;
  }

  return true;

 Fail:
  // TODO(dje): Resume original ktracing?
  mx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_STOP, 0, nullptr);
  mx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_START, 0, nullptr);

  return false;
}

bool StartCpuPerf(const IptConfig& config) {
  FTL_LOG(INFO) << "StartCpuPerf called";
  FTL_DCHECK(config.mode == IPT_MODE_CPUS);

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return false;

  ssize_t ssize = ioctl_ipt_cpu_mode_start(ipt_fd.get());
  if (ssize < 0) {
    util::LogErrorWithMxStatus("start cpu perf", ssize);
    ioctl_ipt_cpu_mode_free(ipt_fd.get());
    goto Fail;
  }

  return true;

 Fail:
  return false;
}

bool StartThreadPerf(Thread* thread, const IptConfig& config) {
  FTL_LOG(INFO) << "StartThreadPerf called";
  FTL_DCHECK(config.mode == IPT_MODE_THREADS);

  if (thread->ipt_buffer() < 0) {
    FTL_LOG(INFO) << ftl::StringPrintf("Thread %" PRId64 " has no IPT buffer",
                                       thread->id());
    // TODO(dje): For now. This isn't an error in the normal sense.
    return true;
  }

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return false;

  ioctl_ipt_assign_buffer_thread_t assign;
  mx_status_t status;
  ssize_t ssize;

  status = mx_handle_duplicate(thread->handle(), MX_RIGHT_SAME_RIGHTS,
                               &assign.thread);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("duplicating thread handle", status);
    goto Fail;
  }
  assign.descriptor = thread->ipt_buffer();
  ssize = ioctl_ipt_assign_buffer_thread(ipt_fd.get(), &assign);
  if (ssize < 0) {
    util::LogErrorWithMxStatus("assigning ipt buffer to thread", ssize);
    goto Fail;
  }

  return true;

 Fail:
  return false;
}

void StopCpuPerf(const IptConfig& config) {
  FTL_LOG(INFO) << "StopCpuPerf called";
  FTL_DCHECK(config.mode == IPT_MODE_CPUS);

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  ssize_t ssize = ioctl_ipt_cpu_mode_stop(ipt_fd.get());
  if (ssize < 0) {
    // TODO(dje): This is really bad, this shouldn't fail.
    util::LogErrorWithMxStatus("stop cpu perf", ssize);
  }
}

void StopThreadPerf(Thread* thread, const IptConfig& config) {
  FTL_LOG(INFO) << "StopThreadPerf called";
  FTL_DCHECK(config.mode == IPT_MODE_THREADS);

  if (thread->ipt_buffer() < 0) {
    FTL_LOG(INFO) << ftl::StringPrintf("Thread %" PRId64 " has no IPT buffer",
                                       thread->id());
    return;
  }

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  ioctl_ipt_assign_buffer_thread_t assign;
  mx_handle_t status;
  ssize_t ssize;

  status = mx_handle_duplicate(thread->handle(), MX_RIGHT_SAME_RIGHTS,
                               &assign.thread);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("duplicating thread handle", status);
    goto Fail;
  }
  assign.descriptor = thread->ipt_buffer();
  ssize = ioctl_ipt_release_buffer_thread(ipt_fd.get(), &assign);
  if (ssize < 0) {
    util::LogErrorWithMxStatus("releasing ipt buffer from thread", ssize);
    goto Fail;
  }

 Fail:
  ; // nothing to do
}

void StopPerf(const IptConfig& config) {
  FTL_LOG(INFO) << "StopPerf called";

  mx::handle ktrace_handle;
  if (!OpenDevices(nullptr, nullptr, &ktrace_handle))
    return;

  mx_status_t status =
    mx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_STOP, 0, nullptr);
  if (status != NO_ERROR) {
    // TODO(dje): This shouldn't fail either, should it?
    util::LogErrorWithMxStatus("stop ktrace", status);
  }
}

// Write the contents of buffer |descriptor| to a file.
// The file's name is output_prefix.buffer.pt.

static mx_status_t WriteBufferData(const IptConfig& config,
                                   const ftl::UniqueFD& ipt_fd,
                                   uint32_t descriptor,
                                   const char* output_prefix) {
  std::string output_path =
    ftl::StringPrintf("%s.%u.pt", output_prefix, descriptor);
  const char* c_path = output_path.c_str();

  mx_status_t status = NO_ERROR;

  // Refetch the buffer config as we can be invoked in a separate process,
  // after tracing has started, and shouldn't rely on what the user thinks
  // the config is.
  ioctl_ipt_buffer_config_t buffer_config;
  ssize_t ssize = ioctl_ipt_get_buffer_config(ipt_fd.get(), &descriptor,
                                              &buffer_config);
  if (ssize < 0) {
    util::LogErrorWithMxStatus(
      ftl::StringPrintf("ioctl_ipt_get_buffer_config: buffer %u", descriptor),
      ssize);
    return ssize;
  }

  ioctl_ipt_buffer_info_t info;
  ssize = ioctl_ipt_get_buffer_info(ipt_fd.get(), &descriptor, &info);
  if (ssize < 0) {
    util::LogErrorWithMxStatus(
      ftl::StringPrintf("ioctl_ipt_get_buffer_info: buffer %u", descriptor),
      ssize);
    return ssize;
  }

  ftl::UniqueFD fd(open(c_path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR));
  if (!fd.is_valid()) {
    util::LogErrorWithErrno(ftl::StringPrintf("unable to write file: %s",
                                              c_path));
    return ERR_BAD_PATH;
  }

  // TODO(dje): Fetch from vmo?
  size_t buffer_size = (1 << buffer_config.buffer_order) * PAGE_SIZE;

  // If using a circular buffer there's (currently) no way to know if
  // tracing wrapped, so for now we just punt and always dump the entire
  // buffer. It's highly likely it wrapped anyway.
  size_t bytes_left;
  if (buffer_config.is_circular)
    bytes_left = buffer_config.num_buffers * buffer_size;
  else
    bytes_left = info.capture_end;

  char buf[4096];

  for (uint32_t i = 0; i < buffer_config.num_buffers && bytes_left > 0; ++i) {
    ioctl_ipt_buffer_handle_req_t handle_rqst;
    handle_rqst.descriptor = descriptor;
    handle_rqst.buffer_num = i;
    mx_handle_t vmo_handle;
    ssize = ioctl_ipt_get_buffer_handle(ipt_fd.get(), &handle_rqst,
                                        &vmo_handle);
    if (ssize < 0) {
      util::LogErrorWithMxStatus(
        ftl::StringPrintf("ioctl_ipt_get_buffer_handle: buffer %u, buffer %u",
                          descriptor, i),
        ssize);
      goto Fail;
    }
    mx::vmo vmo(vmo_handle);

    size_t buffer_remaining = buffer_size;
    size_t offset = 0;
    while (buffer_remaining && bytes_left) {
      size_t to_write = sizeof(buf);
      if (to_write > buffer_remaining)
        to_write = buffer_remaining;
      if (to_write > bytes_left)
        to_write = bytes_left;
      size_t actual;
      // TODO(dje): Mapping into process and reading directly from that
      // left for another day.
      status = vmo.read(buf, offset, to_write, &actual);
      if (status != NO_ERROR) {
        util::LogErrorWithMxStatus(
          ftl::StringPrintf("mx_vmo_read: buffer %u, buffer %u, offset %zu",
                            descriptor, i, offset),
          status);
        goto Fail;
      }
      if (write(fd.get(), buf, to_write) != (ssize_t) to_write) {
        util::LogError(ftl::StringPrintf("short write, file: %s\n", c_path));
        status = ERR_IO;
        goto Fail;
      }
      offset += to_write;
      buffer_remaining -= to_write;
      bytes_left -= to_write;
    }
  }

  assert(bytes_left == 0);
  status = NO_ERROR;
  // fallthrough

 Fail:
  // We don't delete the file on failure on purpose, it is kept for
  // debugging purposes.
  return status;
}

// Write all output files.
// This assumes tracing has already been stopped.

void DumpCpuPerf(const IptConfig& config) {
  FTL_LOG(INFO) << "DumpCpuPerf called";
  FTL_DCHECK(config.mode == IPT_MODE_CPUS);

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  for (uint32_t cpu = 0; cpu < config.num_cpus; ++cpu) {
    auto status = WriteBufferData(config, ipt_fd, cpu, pt_output_path_prefix);
    if (status != NO_ERROR) {
      util::LogErrorWithMxStatus(ftl::StringPrintf("dump perf of cpu %u", cpu),
                                 status);
      // Keep trying to dump other cpu's data.
    }
  }
}

// Write the buffer contents for |thread|.
// This assumes the thread is stopped.

void DumpThreadPerf(Thread* thread, const IptConfig& config) {
  FTL_LOG(INFO) << "DumpThreadPerf called";
  FTL_DCHECK(config.mode == IPT_MODE_THREADS);

  if (thread->ipt_buffer() < 0) {
    FTL_LOG(INFO) << ftl::StringPrintf("Thread %" PRId64 " has no IPT buffer",
                                       thread->id());
    return;
  }

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  uint32_t descriptor = thread->ipt_buffer();
  auto status = WriteBufferData(config, ipt_fd, descriptor,
                                pt_output_path_prefix);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus(
      ftl::StringPrintf("dump perf of thread buffer %u", descriptor),
      status);
  }
}

void DumpPerf(const IptConfig& config) {
  FTL_LOG(INFO) << "DumpPerf called";

  {
    ftl::UniqueFD ktrace_fd;
    if (!OpenDevices(nullptr, &ktrace_fd, nullptr))
      return;

    ftl::UniqueFD dest_fd(open(ktrace_output_path, O_CREAT | O_TRUNC | O_RDWR,
                               S_IRUSR | S_IWUSR));
    if (dest_fd.is_valid()) {
      ssize_t count;
      char buf[1024];
      while ((count = read(ktrace_fd.get(), buf, sizeof(buf))) != 0) {
        if (write(dest_fd.get(), buf, count) != count) {
          FTL_LOG(ERROR) << "error writing " << ktrace_output_path;
        }
      }
    } else {
      util::LogErrorWithErrno(ftl::StringPrintf("unable to create %s",
                                                ktrace_output_path));
    }
  }

  // TODO(dje): UniqueFILE?
  {
    FILE* f = fopen(cpuid_output_path, "w");
    if (f != nullptr) {
      arch::DumpArch(f);
      // Also put the mtc_freq value in the cpuid file, it's as good a place
      // for it as any. See intel-pt.h:pt_config.
      // Alternatively this could be added to the ktrace record.
      // TODO(dje): Put constants in magenta/device/intel-pt.h.
      unsigned mtc_freq = (config.ctl_config & IPT_CTL_MTC_FREQ) >> 14;
      fprintf(f, "mtc_freq: %u\n", mtc_freq);
      fclose(f);
    } else {
      FTL_LOG(ERROR) << "unable to write PT config to " << cpuid_output_path;
    }
  }
}

// Reset perf collection to its original state.
// This means freeing all PT resources.
// This assumes tracing has already been stopped.

void ResetCpuPerf(const IptConfig& config) {
  FTL_LOG(INFO) << "ResetCpuPerf called";
  FTL_DCHECK(config.mode == IPT_MODE_CPUS);

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  ssize_t ssize = ioctl_ipt_cpu_mode_free(ipt_fd.get());
  if (ssize < 0) {
    util::LogErrorWithMxStatus("end perf", ssize);
  }
}

void ResetThreadPerf(Thread* thread, const IptConfig& config) {
  FTL_LOG(INFO) << "ResetThreadPerf called";
  FTL_DCHECK(config.mode == IPT_MODE_THREADS);

  if (thread->ipt_buffer() < 0) {
    FTL_LOG(INFO) << ftl::StringPrintf("Thread %" PRId64 " has no IPT buffer",
                                       thread->id());
    return;
  }

  ftl::UniqueFD ipt_fd;
  if (!OpenDevices(&ipt_fd, nullptr, nullptr))
    return;

  uint32_t descriptor = thread->ipt_buffer();
  ssize_t ssize = ioctl_ipt_free_buffer(ipt_fd.get(), &descriptor);
  if (ssize < 0) {
    util::LogErrorWithMxStatus("freeing ipt buffer", ssize);
    goto Fail;
  }

 Fail:
  thread->set_ipt_buffer(-1);
}

// Reset perf collection to its original state.
// This means restoring ktrace to its original state.
// This assumes tracing has already been stopped.

void ResetPerf(const IptConfig& config) {
  FTL_LOG(INFO) << "ResetPerf called";

  ftl::UniqueFD ipt_fd;
  mx::handle ktrace_handle;
  if (!OpenDevices(&ipt_fd, nullptr, &ktrace_handle))
    return;

  // FIXME(dje): Workaround to switching from thread mode to cpu mode:
  // xrstors gets a gpf -> panic.
  uint32_t mode = IPT_MODE_CPUS;
  ssize_t ssize = ioctl_ipt_set_mode(ipt_fd.get(), &mode);
  if (ssize < 0)
    util::LogErrorWithMxStatus("reset perf mode", ssize);

  // TODO(dje): Resume original ktracing?
  mx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_STOP, 0, nullptr);
  mx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_REWIND, 0, nullptr);
  mx_ktrace_control(ktrace_handle.get(), KTRACE_ACTION_START, 0, nullptr);
}

} // debugserver namespace
