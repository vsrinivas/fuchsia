// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/elf-psabi/sp.h>
#include <lib/processargs/processargs.h>
#include <lib/stdcompat/source_location.h>
#include <lib/userabi/userboot.h>
#include <lib/zircon-internal/default_stack_size.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <sys/param.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/resource.h>
#include <zircon/syscalls/system.h>

#include <array>
#include <climits>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

#include "bootfs.h"
#include "loader-service.h"
#include "option.h"
#include "userboot-elf.h"
#include "util.h"
#include "zbi.h"

namespace {

constexpr const char kStackVmoName[] = "userboot-child-initial-stack";

using namespace userboot;

[[noreturn]] void DoPowerctl(const zx::debuglog& log, const zx::resource& power_rsrc,
                             uint32_t reason) {
  const char* r_str = (reason == ZX_SYSTEM_POWERCTL_SHUTDOWN) ? "poweroff" : "reboot";
  if (reason == ZX_SYSTEM_POWERCTL_REBOOT) {
    printl(log, "Waiting 3 seconds...");
    zx::nanosleep(zx::deadline_after(zx::sec(3)));
  }

  printl(log, "Process exited.  Executing \"%s\".", r_str);
  zx_system_powerctl(power_rsrc.get(), reason, nullptr);
  printl(log, "still here after %s!", r_str);
  while (true)
    __builtin_trap();
}

void LoadChildProcess(const zx::debuglog& log, const Options& opts, std::string_view filename,
                      Bootfs& bootfs, const zx::vmo& vdso_vmo, const zx::process& proc,
                      const zx::vmar& vmar, const zx::thread& thread, const zx::channel& to_child,
                      zx_vaddr_t* entry, zx_vaddr_t* vdso_base, size_t* stack_size,
                      zx::channel* loader_svc) {
  // Examine the bootfs image and find the requested file in it.
  // This will handle a PT_INTERP by doing a second lookup in bootfs.
  *entry = elf_load_bootfs(log, bootfs, opts.root, proc, vmar, thread, filename, to_child,
                           stack_size, loader_svc);
  // Now load the vDSO into the child, so it has access to system calls.
  *vdso_base = elf_load_vdso(log, vmar, vdso_vmo);
}

// Reserve roughly the low half of the address space, so the initial
// process can use sanitizers that need to allocate shadow memory there.
// The reservation VMAR is kept around just long enough to make sure all
// the initial allocations (mapping in the initial ELF object, and
// allocating the initial stack) stay out of this area, and then destroyed.
// The process's own allocations can then use the full address space; if
// it's using a sanitizer, it will set up its shadow memory first thing.
zx::vmar ReserveLowAddressSpace(const zx::debuglog& log, const zx::vmar& root_vmar) {
  zx_info_vmar_t info;
  check(log, root_vmar.get_info(ZX_INFO_VMAR, &info, sizeof(info), nullptr, nullptr),
        "zx_object_get_info failed on child root VMAR handle");
  zx::vmar vmar;
  uintptr_t addr;
  size_t reserve_size = (((info.base + info.len) / 2) + zx_system_get_page_size() - 1) &
                        -static_cast<uint64_t>(zx_system_get_page_size());
  zx_status_t status =
      root_vmar.allocate(ZX_VM_SPECIFIC, 0, reserve_size - info.base, &vmar, &addr);
  check(log, status, "zx_vmar_allocate failed for low address space reservation");
  if (addr != info.base) {
    fail(log, "zx_vmar_allocate gave wrong address?!?");
  }
  return vmar;
}

void ParseNextProcessArguments(const zx::debuglog& log, const Options& opts, uint32_t& argc,
                               char* argv) {
  // Extra byte for null terminator.
  size_t required_size = opts.next.size() + 1;
  if (required_size > kProcessArgsMaxBytes) {
    fail(log, "required %zu bytes for process arguments, but only %u are available", required_size,
         kProcessArgsMaxBytes);
  }

  // At a minimum, child processes will be passed a single argument containing the binary name.
  argc++;
  uint32_t index = 0;
  for (char c : opts.next) {
    if (c == '+') {
      // Argument list is provided as '+' separated, but passed as null separated. Every time
      // we encounter a '+' we replace it with a null and increment the argument counter.
      argv[index] = '\0';
      argc++;
    } else {
      argv[index] = c;
    }
    index++;
  }

  argv[index] = '\0';
}

std::string_view GetUserbootNextFilename(const Options& opts) {
  return opts.next.substr(0, opts.next.find_first_of('+'));
}

// We don't need our own thread handle, but the child does. In addition we pass on a decompressed
// BOOTFS VMO, and a debuglog handle (tied to stdout).
//
// In total we're passing along three more handles than we got.
constexpr uint32_t kThreadSelf = kHandleCount;
constexpr uint32_t kBootfsVmo = kHandleCount + 1;
constexpr uint32_t kDebugLog = kHandleCount + 2;
constexpr uint32_t kChildHandleCount = kHandleCount + 3;

// This is the processargs message the child will receive.
struct ChildMessageLayout {
  zx_proc_args_t header{};
  std::array<char, kProcessArgsMaxBytes> args;
  std::array<uint32_t, kChildHandleCount> info;
};

static_assert(alignof(std::array<uint32_t, kChildHandleCount>) ==
              alignof(uint32_t[kChildHandleCount]));
static_assert(alignof(std::array<char, kChildHandleCount>) == alignof(char[kProcessArgsMaxBytes]));

constexpr std::array<uint32_t, kChildHandleCount> HandleInfoTable() {
  std::array<uint32_t, kChildHandleCount> info = {};
  // Fill in the handle info table.
  info[kBootfsVmo] = PA_HND(PA_VMO_BOOTFS, 0);
  info[kProcSelf] = PA_HND(PA_PROC_SELF, 0);
  info[kRootJob] = PA_HND(PA_JOB_DEFAULT, 0);
  info[kRootResource] = PA_HND(PA_RESOURCE, 0);
  info[kMmioResource] = PA_HND(PA_MMIO_RESOURCE, 0);
  info[kIrqResource] = PA_HND(PA_IRQ_RESOURCE, 0);
#if __x86_64__
  info[kIoportResource] = PA_HND(PA_IOPORT_RESOURCE, 0);
#elif __aarch64__
  info[kSmcResource] = PA_HND(PA_SMC_RESOURCE, 0);
#endif
  info[kSystemResource] = PA_HND(PA_SYSTEM_RESOURCE, 0);
  info[kThreadSelf] = PA_HND(PA_THREAD_SELF, 0);
  info[kVmarRootSelf] = PA_HND(PA_VMAR_ROOT, 0);
  info[kZbi] = PA_HND(PA_VMO_BOOTDATA, 0);
  for (uint32_t i = kFirstVdso; i <= kLastVdso; ++i) {
    info[i] = PA_HND(PA_VMO_VDSO, i - kFirstVdso);
  }
  for (uint32_t i = kFirstKernelFile; i < kHandleCount; ++i) {
    info[i] = PA_HND(PA_VMO_KERNEL_FILE, i - kFirstKernelFile);
  }
  info[kDebugLog] = PA_HND(PA_FD, kFdioFlagUseForStdio);
  return info;
}

constexpr ChildMessageLayout CreateChildMessage() {
  ChildMessageLayout child_message = {
      .header =
          {
              .protocol = ZX_PROCARGS_PROTOCOL,
              .version = ZX_PROCARGS_VERSION,
              .handle_info_off = offsetof(ChildMessageLayout, info),
              .args_off = offsetof(ChildMessageLayout, args),
          },
      .info = HandleInfoTable(),
  };

  return child_message;
}

std::array<zx_handle_t, kChildHandleCount> ExtractHandles(zx::channel bootstrap) {
  // Default constructed debuglog will force check/fail to fallback to |zx_debug_write|.
  zx::debuglog log;
  // Read the command line and the essential handles from the kernel.
  std::array<zx_handle_t, kChildHandleCount> handles = {};
  uint32_t actual_handles;
  zx_status_t status =
      bootstrap.read(0, nullptr, handles.data(), 0, handles.size(), nullptr, &actual_handles);

  check(log, status, "cannot read bootstrap message");
  if (actual_handles != kHandleCount) {
    fail(log, "read %u handles instead of %u", actual_handles, kHandleCount);
  }
  return handles;
}

template <typename T>
T DuplicateOrDie(const zx::debuglog& log, const T& typed_handle,
                 unsigned int line = cpp20::source_location::current().line()) {
  T dup;
  auto status = typed_handle.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  check(log, status, "[start.cc:%d]: Failed to duplicate handle.", line);
  return dup;
}

zx::debuglog DuplicateOrDie(const zx::debuglog& log,
                            unsigned int line = cpp20::source_location::current().line()) {
  return DuplicateOrDie(log, log, line);
}

struct ChildContext {
  // Process creation handles
  zx::process process;
  zx::vmar vmar;
  zx::vmar reserved_vmar;
  zx::thread thread;

  // to_child and bootstrap are peers, such that the child process receives the |bootstrap|
  // end and userboot uses |to_child| to send the process arguments.

  // Used to send initial message to the child with the process protocol,
  // including handles.
  zx::channel to_child;

  // Handed off when the process starts, so it can bootstrap itself.
  zx::channel bootstrap;
};

ChildContext CreateChildContext(const zx::debuglog& log, std::string_view name,
                                cpp20::span<const zx_handle_t> handles) {
  ChildContext child;
  auto status =
      zx::process::create(*zx::unowned_job{handles[kRootJob]}, name.data(),
                          static_cast<uint32_t>(name.size()), 0, &child.process, &child.vmar);
  check(log, status, "Failed to create child process(%.*s).", static_cast<int>(name.length()),
        name.data());

  // Squat on some address space before we start loading it up.
  child.reserved_vmar = {ReserveLowAddressSpace(log, child.vmar)};

  // Create the initial thread in the new process
  status = zx::thread::create(child.process, name.data(), static_cast<uint32_t>(name.size()), 0,
                              &child.thread);
  check(log, status, "Failed to create main thread for child process(%.*s).",
        static_cast<int>(name.length()), name.data());

  status = zx::channel::create(0, &child.to_child, &child.bootstrap);
  check(log, status, "Failed to create bootstrap channels for child process(%.*s).",
        static_cast<int>(name.length()), name.data());
  return child;
}

void SetChildHandles(const zx::debuglog& log, const ChildContext& child, const zx::vmo& bootfs_vmo,
                     cpp20::span<zx_handle_t, kChildHandleCount> child_handles) {
  child_handles[kBootfsVmo] = DuplicateOrDie(log, bootfs_vmo).release();
  child_handles[kDebugLog] = DuplicateOrDie(log).release();
  child_handles[kProcSelf] = DuplicateOrDie(log, child.process).release();
  child_handles[kVmarRootSelf] = DuplicateOrDie(log, child.vmar).release();
  child_handles[kThreadSelf] = DuplicateOrDie(log, child.thread).release();

  // Verify all child handles.
  for (size_t i = 0; i < child_handles.size(); ++i) {
    auto handle = child_handles[i];
    zx_info_handle_basic_t info;
    auto status =
        zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    check(log, status, "Failed to obtain handle information. Bad handle at %zu with value %x", i,
          handle);
  }
}

// This is the main logic:
// 1. Read the kernel's bootstrap message.
// 2. Load up the child process from ELF file(s) on the bootfs.
// 3. Create the initial thread and allocate a stack for it.
// 4. Load up a channel with the zx_proc_args_t message for the child.
// 5. Start the child process running.
// 6. Optionally, wait for it to exit and then shut down.
[[noreturn]] void Bootstrap(zx::channel channel) {
  // We pass all the same handles the kernel gives us along to the child,
  // except replacing our own process/root-VMAR handles with its, and
  // passing along the three extra handles (BOOTFS, thread-self, and a debuglog
  // handle tied to stdout).
  std::array<zx_handle_t, kChildHandleCount> handles = ExtractHandles(std::move(channel));

  // Now that we have the root resource, we can use it to get a debuglog.
  zx::debuglog log;
  auto status = zx::debuglog::create(*zx::unowned_resource{handles[kRootResource]}, 0, &log);
  check(log, status, "zx_debuglog_create failed: %d", status);

  // We need our own root VMAR handle to map in the ZBI.
  zx::vmar vmar_self{handles[kVmarRootSelf]};
  handles[kVmarRootSelf] = ZX_HANDLE_INVALID;

  // Hang on to our own process handle.  If we closed it, our process
  // would be killed.  Exiting will clean it up.
  zx::process proc_self{handles[kProcSelf]};
  handles[kProcSelf] = ZX_HANDLE_INVALID;

  // Locate the ZBI_TYPE_STORAGE_BOOTFS item and decompress it. This will be used to load
  // the binary referenced by userboot.next, as well as libc. Bootfs will be fully parsed
  // and hosted under '/boot' either by bootsvc or component manager.
  const zx::unowned_vmo zbi{handles[kZbi]};
  zx::vmo bootfs_vmo = GetBootfsFromZbi(log, vmar_self, *zbi);

  // Parse CMDLINE items to determine the set of runtime options.
  Options opts = GetOptionsFromZbi(log, vmar_self, *zbi);

  // Get the power resource handle in case we call powerctl below.
  zx::resource power_resource;
  zx::unowned_resource system_resource(handles[kSystemResource]);
  status = zx::resource::create(*system_resource, ZX_RSRC_KIND_SYSTEM, ZX_RSRC_SYSTEM_POWER_BASE, 1,
                                nullptr, 0, &power_resource);
  check(log, status, "zx_resource_create");

  // Strips any arguments passed along with the filename to userboot.next.
  std::string_view filename = GetUserbootNextFilename(opts);

  ChildMessageLayout child_message = CreateChildMessage();
  ChildContext child = CreateChildContext(log, filename, handles);
  SetChildHandles(log, child, bootfs_vmo, handles);

  // Fill in any '+' separated arguments provided by `userboot.next`. If arguments are longer than
  // kProcessArgsMaxBytes, this function will fail process creation.
  ParseNextProcessArguments(log, opts, child_message.header.args_num, child_message.args.data());

  {
    // Map in the bootfs so we can look for files in it.
    zx::resource vmex_resource;
    zx::unowned_resource system_resource(handles[kSystemResource]);
    status = zx::resource::create(*system_resource, ZX_RSRC_KIND_SYSTEM, ZX_RSRC_SYSTEM_VMEX_BASE,
                                  1, nullptr, 0, &vmex_resource);
    check(log, status, "zx_resource_create failed");
    Bootfs bootfs{vmar_self.borrow(), std::move(bootfs_vmo), std::move(vmex_resource),
                  DuplicateOrDie(log)};

    // Map in the code.
    zx_vaddr_t entry, vdso_base;
    size_t stack_size = ZIRCON_DEFAULT_STACK_SIZE;
    zx::channel loader_service_channel;
    LoadChildProcess(log, opts, filename, bootfs, *zx::unowned_vmo{handles[kFirstVdso]},
                     child.process, child.vmar, child.thread, child.to_child, &entry, &vdso_base,
                     &stack_size, &loader_service_channel);

    // Allocate the stack for the child.
    uintptr_t sp;
    {
      stack_size = (stack_size + zx_system_get_page_size() - 1) &
                   -static_cast<uint64_t>(zx_system_get_page_size());
      zx::vmo stack_vmo;
      status = zx::vmo::create(stack_size, 0, &stack_vmo);
      check(log, status, "zx_vmo_create failed for child stack");
      stack_vmo.set_property(ZX_PROP_NAME, kStackVmoName, sizeof(kStackVmoName) - 1);
      zx_vaddr_t stack_base;
      status = child.vmar.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, stack_vmo, 0, stack_size,
                              &stack_base);
      check(log, status, "zx_vmar_map failed for child stack");
      sp = compute_initial_stack_pointer(stack_base, stack_size);
      printl(log, "stack [%p, %p) sp=%p", reinterpret_cast<void*>(stack_base),
             reinterpret_cast<void*>(stack_base + stack_size), reinterpret_cast<void*>(sp));
    }

    // We're done doing mappings, so clear out the reservation VMAR.
    check(log, child.reserved_vmar.destroy(), "zx_vmar_destroy failed on reservation VMAR handle");
    child.reserved_vmar.reset();

    // Now send the bootstrap message.  This transfers away all the handles
    // we have left except the process and thread themselves.
    status = child.to_child.write(0, &child_message, sizeof(child_message), handles.data(),
                                  handles.size());
    check(log, status, "zx_channel_write to child failed");
    child.to_child.reset();

    // Start the process going.
    status = child.process.start(child.thread, entry, sp, std::move(child.bootstrap), vdso_base);
    check(log, status, "zx_process_start failed");
    child.thread.reset();

    printl(log, "process %.*s started.", static_cast<int>(filename.size()), filename.data());

    // Now become the loader service for as long as that's needed.
    if (loader_service_channel) {
      LoaderService ldsvc(DuplicateOrDie(log), &bootfs, opts.root);
      ldsvc.Serve(std::move(loader_service_channel));
    }

    // All done with bootfs! Let it go out of scope.
  }

  auto wait_till_child_exits = [child_name = filename, &log, &child]() {
    printl(log, "Waiting for %.*s to exit...", static_cast<int>(child_name.size()),
           child_name.data());
    zx_status_t status =
        child.process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
    check(log, status, "zx_object_wait_one on process failed");
    zx_info_process_t info;
    status = child.process.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    check(log, status, "zx_object_get_info on process failed");
    printl(log, "*** Exit status %zd ***\n", info.return_code);
    if (info.return_code == 0) {
      // The test runners match this exact string on the console log
      // to determine that the test succeeded since shutting the
      // machine down doesn't return a value to anyone for us.
      printl(log, "%s\n", ZBI_TEST_SUCCESS_STRING);
    }
  };

  // Now we've accomplished our purpose in life, and we can die happy.
  switch (opts.epilogue) {
    case Epilogue::kExitAfterChildLaunch:
      child.process.reset();
      printl(log, "finished!");
      zx_process_exit(0);
    case Epilogue::kRebootAfterChildExit:
      wait_till_child_exits();
      DoPowerctl(log, power_resource, ZX_SYSTEM_POWERCTL_REBOOT);
    case Epilogue::kPowerOffAfterChildExit:
      wait_till_child_exits();
      DoPowerctl(log, power_resource, ZX_SYSTEM_POWERCTL_SHUTDOWN);
  }

  __UNREACHABLE;
}

}  // anonymous namespace

// This is the entry point for the whole show, the very first bit of code
// to run in user mode.
extern "C" [[noreturn]] void _start(zx_handle_t arg) { Bootstrap(zx::channel{arg}); }
