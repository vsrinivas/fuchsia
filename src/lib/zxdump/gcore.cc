// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <lib/zxdump/dump.h>
#include <lib/zxdump/fd-writer.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <fbl/unique_fd.h>
#include <task-utils/get.h>

namespace {

using namespace std::literals;

// Command-line flags controlling the dump are parsed into this object, which
// is passed around to the methods affected by policy choices.
struct Flags {
  std::string OutputFile(zx_koid_t pid) const {
    std::string filename{output_prefix_ ? *output_prefix_ : "core."sv};
    filename += std::to_string(pid);
    return filename;
  }

  std::optional<std::string_view> output_prefix_;
  size_t limit_ = zxdump::DefaultLimit();
  bool dump_memory_ = true;
};

// This handles writing a single output file, and removing that output file if
// the dump is aborted before `Ok(true)` is called.
class Writer : public zxdump::FdWriter {
 public:
  Writer(fbl::unique_fd fd, std::string filename)
      : zxdump::FdWriter{std::move(fd)}, filename_{std::move(filename)} {}

  // Write errors from errno use the file name.
  void Error(std::string_view op) {
    ZX_DEBUG_ASSERT(!op.empty());
    std::string_view fn = filename_;
    if (fn.empty()) {
      fn = "<stdout>"sv;
    }
    std::cerr << fn << ": "sv << op << ": "sv << strerror(errno) << std::endl;
  }

  // Called with true if the output file should be preserved at destruction.
  bool Ok(bool ok) {
    if (ok) {
      filename_.clear();
    }
    return ok;
  }

  ~Writer() {
    if (!filename_.empty()) {
      remove(filename_.c_str());
    }
  }

 private:
  std::string filename_;
};

// This handles collecting and producing the dump for one process.
// The Writer and Flags objects get passed in to control the details
// and of the dump and where it goes.
class ProcessDumper {
 public:
  static fitx::result<zxdump::Error, zxdump::SegmentDisposition> PruneAll(
      zxdump::SegmentDisposition segment, const zx_info_maps_t& mapping, const zx_info_vmo_t& vmo) {
    segment.filesz = 0;
    return fitx::ok(segment);
  }

  static fitx::result<zxdump::Error, zxdump::SegmentDisposition> PruneDefault(
      zxdump::SegmentDisposition segment, const zx_info_maps_t& mapping, const zx_info_vmo_t& vmo) {
    if (mapping.u.mapping.committed_pages == 0 &&   // No private RAM here,
        vmo.parent_koid == ZX_KOID_INVALID &&       // and none shared,
        !(vmo.flags & ZX_INFO_VMO_PAGER_BACKED)) {  // and no backing store.
      // Since it's not pager-backed, there isn't data hidden in backing
      // store.  If we read this, it would just be zero-fill anyway.
      segment.filesz = 0;
    }

    // TODO(mcgrathr): for now, dump everything else.

    return fitx::ok(segment);
  }

  ProcessDumper(zx::unowned_process process, zx_koid_t pid)
      : dumper_{std::move(process)}, pid_(pid) {}

  // Read errors from syscalls use the PID.
  void Error(const zxdump::Error& error) const {
    std::cerr << pid_ << ": "sv << error << std::endl;
  }

  // Phase 1: Collect underpants!
  bool Collect(const Flags& flags) {
    zxdump::SegmentCallback prune = PruneAll;
    if (flags.dump_memory_) {
      // TODO(mcgrathr): more filtering switches
      prune = PruneDefault;
    }
    auto result = dumper_.CollectProcess(std::move(prune), flags.limit_);
    if (result.is_error()) {
      Error(result.error_value());
      return false;
    }
    return true;
  }

  // Phase 2: ???
  bool Dump(Writer& writer, const Flags& flags) {
    // File offset calculations start fresh in each ET_CORE file.
    writer.ResetOffset();

    // Now write the accumulated header data first: not including the memory.
    // These iovecs will point into storage in the ProcessDump object itself.
    size_t total;
    if (auto result = dumper_.DumpHeaders(writer.AccumulateFragmentsCallback(), flags.limit_);
        result.is_error()) {
      Error(result.error_value());
      return false;
    } else {
      total = result.value();
    }

    if (total > flags.limit_) {
      errno = EFBIG;
      writer.Error("not written");
      return false;
    }

    if (auto result = writer.WriteFragments(); result.is_error()) {
      writer.Error(result.error_value());
      return false;
    }

    if (auto memory = dumper_.DumpMemory(writer.WriteCallback(), flags.limit_); memory.is_error()) {
      Error(memory.error_value());
      return false;
    }

    return true;
  }

 private:
  zxdump::ProcessDump<zx::unowned_process> dumper_;
  zx_koid_t pid_;
};

fbl::unique_fd CreateOutputFile(const std::string& outfile) {
  fbl::unique_fd fd{open(outfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0666)};
  if (!fd) {
    perror(outfile.c_str());
  }
  return fd;
}

// Phase 3: Profit!
bool WriteProcessCoreFile(zx::process process, zx_koid_t pid, const Flags& flags) {
  std::string outfile = flags.OutputFile(pid);
  fbl::unique_fd fd = CreateOutputFile(outfile);
  if (!fd) {
    return false;
  }
  Writer writer{std::move(fd), std::move(outfile)};
  ProcessDumper dumper{zx::unowned_process{process}, pid};
  return writer.Ok(dumper.Collect(flags) && dumper.Dump(writer, flags));
}

constexpr const char kOptString[] = "hlo:m";
constexpr const option kLongOpts[] = {
    {"help", no_argument, nullptr, 'h'},                 //
    {"limit", required_argument, nullptr, 'l'},          //
    {"output-prefix", required_argument, nullptr, 'o'},  //
    {"exclude-memory", no_argument, nullptr, 'm'},       //
    {nullptr, no_argument, nullptr, 0},                  //
};

}  // namespace

int main(int argc, char** argv) {
  Flags flags;

  auto usage = [&](int status = EXIT_FAILURE) {
    std::cerr << "Usage: " << argv[0] << R"""( [SWITCHES...] PID...
    --help, -h                         print this message
    --output-prefix=PREFIX, -o PREFIX  write <PREFIX><PID>, not core.<PID>
    --limit=BYTES, -l BYTES            truncate output to BYTES per process
    --exclude-memory, -M               exclude all process memory from dumps
)""";
    return status;
  };

  while (true) {
    switch (getopt_long(argc, argv, kOptString, kLongOpts, nullptr)) {
      case -1:
        // This ends the loop.  All other cases continue (or return).
        break;

      case 'o':
        flags.output_prefix_ = optarg;
        continue;

      case 'l': {
        char* p;
        flags.limit_ = strtoul(optarg, &p, 0);
        if (*p != '\0') {
          return usage();
        }
        continue;
      }

      case 'm':
        flags.dump_memory_ = false;
        continue;

      default:
        return usage(EXIT_SUCCESS);
    }
    break;
  }

  if (optind == argc) {
    return usage();
  }

  int exit_status = EXIT_SUCCESS;
  for (int i = optind; i < argc; ++i) {
    char* p;
    zx_koid_t pid = strtoul(argv[i], &p, 0);
    if (*p != '\0') {
      std::cerr << "Not a PID (KOID): " << argv[i] << std::endl;
      return usage();
    }

    zx_obj_type_t type;
    zx_handle_t handle;
    zx_status_t status = get_task_by_koid(pid, &type, &handle);
    if (status != ZX_OK) {
      std::cerr << pid << ": " << zx_status_get_string(status) << std::endl;
      status = EXIT_FAILURE;
      continue;
    }

    switch (type) {
      default:
        std::cerr << pid << ": KOID is not a process\n";
        zx_handle_close(handle);
        exit_status = EXIT_FAILURE;
        break;

      case ZX_OBJ_TYPE_PROCESS:
        if (!WriteProcessCoreFile(zx::process{handle}, pid, flags)) {
          exit_status = EXIT_FAILURE;
        }
        break;
    }
  }

  return exit_status;
}
