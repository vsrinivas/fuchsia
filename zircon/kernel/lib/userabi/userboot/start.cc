// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fuchsia/boot/c/fidl.h>
#include <lib/elfldltl/machine.h>
#include <lib/fidl/txn_header.h>
#include <lib/processargs/processargs.h>
#include <lib/stdcompat/array.h>
#include <lib/stdcompat/source_location.h>
#include <lib/stdcompat/span.h>
#include <lib/userabi/userboot.h>
#include <lib/zircon-internal/default_stack_size.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <sys/param.h>
#include <zircon/fidl.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/resource.h>
#include <zircon/syscalls/system.h>
#include <zircon/types.h>

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
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

void ParseNextProcessArguments(const zx::debuglog& log, std::string_view next, uint32_t& argc,
                               char* argv) {
  // Extra byte for null terminator.
  size_t required_size = next.size() + 1;
  if (required_size > kProcessArgsMaxBytes) {
    fail(log, "required %zu bytes for process arguments, but only %u are available", required_size,
         kProcessArgsMaxBytes);
  }

  // At a minimum, child processes will be passed a single argument containing the binary name.
  argc++;
  uint32_t index = 0;
  for (char c : next) {
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

// We don't need our own thread handle, but the child does. In addition we pass on a decompressed
// BOOTFS VMO, and a debuglog handle (tied to stdout).
//
// In total we're passing along three more handles than we got.
constexpr uint32_t kThreadSelf = kHandleCount;
constexpr uint32_t kBootfsVmo = kHandleCount + 1;
constexpr uint32_t kDebugLog = kHandleCount + 2;

// Hand a svc channel to the child process to be launched.
// Fuchsia C runtime will pull this handle and automatically create the endpoint on process startup.
constexpr uint32_t kSvcStub = kHandleCount + 3;
constexpr uint32_t kSvcNameIndex = 0;

// A channel containing all 'svc' stubs server ends.
constexpr uint32_t kSvcStash = kHandleCount + 4;

constexpr uint32_t kChildHandleCount = kHandleCount + 5;

// This is the processargs message the child will receive.
struct ChildMessageLayout {
  zx_proc_args_t header{};
  std::array<char, kProcessArgsMaxBytes> args;
  std::array<uint32_t, kChildHandleCount> info;
  std::array<char, 5> names = cpp20::to_array("/svc");
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
  info[kSvcStub] = PA_HND(PA_NS_DIR, kSvcNameIndex);
  info[kSvcStash] = PA_HND(PA_USER0, 0);
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
              .names_off = offsetof(ChildMessageLayout, names),
              .names_num = kSvcNameIndex + 1,

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
                 cpp20::source_location src_loc = cpp20::source_location::current()) {
  T dup;
  auto status = typed_handle.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  check(log, status, "[%s:%d]: Failed to duplicate handle.", src_loc.file_name(), src_loc.line());
  return dup;
}

zx_handle_t RawDuplicateOrDie(const zx::debuglog& log, zx_handle_t handle,
                              cpp20::source_location src_loc = cpp20::source_location::current()) {
  zx_handle_t dup;
  auto status = zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, &dup);
  check(log, status, "[%s:%d]: Failed to duplicate handle.", src_loc.file_name(), src_loc.line());
  return dup;
}

zx::debuglog DuplicateOrDie(const zx::debuglog& log,
                            cpp20::source_location src_loc = cpp20::source_location::current()) {
  return DuplicateOrDie(log, log, src_loc);
}

struct ChildContext {
  ChildContext() = default;
  ChildContext(ChildContext&&) = default;
  ~ChildContext() { zx_handle_close_many(handles.data(), handles.size()); }

  // Process creation handles
  zx::process process;
  zx::vmar vmar;
  zx::vmar reserved_vmar;
  zx::thread thread;

  zx::channel svc_client;
  zx::channel svc_server;

  std::array<zx_handle_t, kChildHandleCount> handles = {};
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

  status = zx::channel::create(0, &child.svc_client, &child.svc_server);
  check(log, status, "Failed to create svc channels.");

  // Copy all resources that are not explicitly duplicated in SetChildHandles.
  for (size_t i = 0; i < handles.size() && i < kHandleCount; ++i) {
    if (i == kProcSelf) {
      continue;
    }

    if (i == kVmarRootSelf) {
      continue;
    }

    if (handles[i] == ZX_HANDLE_INVALID) {
      continue;
    }

    child.handles[i] = RawDuplicateOrDie(log, handles[i]);
  }

  return child;
}

void SetChildHandles(const zx::debuglog& log, const zx::vmo& bootfs_vmo, ChildContext& child) {
  child.handles[kBootfsVmo] = DuplicateOrDie(log, bootfs_vmo).release();
  child.handles[kDebugLog] = DuplicateOrDie(log).release();
  child.handles[kProcSelf] = DuplicateOrDie(log, child.process).release();
  child.handles[kVmarRootSelf] = DuplicateOrDie(log, child.vmar).release();
  child.handles[kThreadSelf] = DuplicateOrDie(log, child.thread).release();
  child.handles[kSvcStub] = child.svc_client.release();

  // Verify all child handles.
  for (size_t i = 0; i < child.handles.size(); ++i) {
    // The stash handle is only passed to the last process launched by userboot.
    if (i == kSvcStash) {
      continue;
    }
    auto handle = child.handles[i];
    zx_info_handle_basic_t info;
    auto status =
        zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    check(log, status, "Failed to obtain handle information. Bad handle at %zu with value %x", i,
          handle);
  }
}

void StashSvc(const zx::debuglog& log, const zx::channel& stash, std::string_view name,
              zx::channel svc_end) {
  zx_handle_t h = svc_end.release();
  fuchsia_boot_SvcStashStoreRequestMessage request = {};
  fidl_init_txn_header(&request.hdr, 0, fuchsia_boot_SvcStashStoreOrdinal, 0);
  request.svc_endpoint = FIDL_HANDLE_PRESENT;

  auto status = stash.write(0, &request, sizeof(request), &h, 1);
  check(log, status, "Failed to stash svc handle from (%.*s)", static_cast<int>(name.length()),
        name.data());
}

void SetStashHandle(const zx::debuglog& log, zx::channel stash,
                    cpp20::span<zx_handle_t, kChildHandleCount> handles) {
  handles[kSvcStash] = stash.release();

  // Check that the handle is valid/alive.
  zx_info_handle_basic_t info;
  auto& handle = handles[kSvcStash];
  auto status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  check(log, status, "Failed to obtain handle information. Bad handle at %d with value %x",
        static_cast<int>(kSvcStash), handle);
}

// Set of resources created in userboot.
struct Resources {
  // Needed for properly implementing the epilogue.
  zx::resource power;

  // Needed for vending executable memory from bootfs.
  zx::resource vmex;
};

Resources CreateResources(const zx::debuglog& log,
                          cpp20::span<const zx_handle_t, kChildHandleCount> handles) {
  Resources resources = {};
  zx::unowned_resource system(handles[kSystemResource]);
  auto status = zx::resource::create(*system, ZX_RSRC_KIND_SYSTEM, ZX_RSRC_SYSTEM_POWER_BASE, 1,
                                     nullptr, 0, &resources.power);
  check(log, status, "Failed to created power resource.");

  status = zx::resource::create(*system, ZX_RSRC_KIND_SYSTEM, ZX_RSRC_SYSTEM_VMEX_BASE, 1, nullptr,
                                0, &resources.vmex);
  check(log, status, "Failed to created vmex resource.");
  return resources;
}

zx::channel StartChildProcess(const zx::debuglog& log, const Options::ProgramInfo& elf_entry,
                              const ChildMessageLayout& child_message, ChildContext& child,
                              Bootfs& bootfs, size_t handle_count) {
  size_t stack_size = ZIRCON_DEFAULT_STACK_SIZE;

  zx::channel to_child, bootstrap;
  auto status = zx::channel::create(0, &to_child, &bootstrap);
  check(log, status, "zx_channel_create failed for child stack");

  zx::channel loader_svc;

  // Examine the bootfs image and find the requested file in it.
  // This will handle a PT_INTERP by doing a second lookup in bootfs.
  zx_vaddr_t entry =
      elf_load_bootfs(log, bootfs, elf_entry.root, child.process, child.vmar, child.thread,
                      elf_entry.filename(), to_child, &stack_size, &loader_svc);

  // Now load the vDSO into the child, so it has access to system calls.
  zx_vaddr_t vdso_base =
      elf_load_vdso(log, child.vmar, *zx::unowned_vmo{child.handles[kFirstVdso]});

  stack_size = (stack_size + zx_system_get_page_size() - 1) &
               -static_cast<uint64_t>(zx_system_get_page_size());
  zx::vmo stack_vmo;
  status = zx::vmo::create(stack_size, 0, &stack_vmo);
  check(log, status, "zx_vmo_create failed for child stack");
  stack_vmo.set_property(ZX_PROP_NAME, kStackVmoName, sizeof(kStackVmoName) - 1);
  zx_vaddr_t stack_base;
  status =
      child.vmar.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, stack_vmo, 0, stack_size, &stack_base);
  check(log, status, "zx_vmar_map failed for child stack");

  // Allocate the stack for the child.
  uintptr_t sp = elfldltl::AbiTraits<>::InitialStackPointer(stack_base, stack_size);
  printl(log, "stack [%p, %p) sp=%p", reinterpret_cast<void*>(stack_base),
         reinterpret_cast<void*>(stack_base + stack_size), reinterpret_cast<void*>(sp));

  // We're done doing mappings, so clear out the reservation VMAR.
  check(log, child.reserved_vmar.destroy(), "zx_vmar_destroy failed on reservation VMAR handle");
  child.reserved_vmar.reset();
  // Now send the bootstrap message.  This transfers away all the handles
  // we have left except the process and thread themselves.
  status = to_child.write(0, &child_message, sizeof(child_message), child.handles.data(),
                          static_cast<uint32_t>(handle_count));
  check(log, status, "zx_channel_write to child failed");

  // Start the process going.
  status = child.process.start(child.thread, entry, sp, std::move(bootstrap), vdso_base);
  check(log, status, "zx_process_start failed");
  child.thread.reset();

  return loader_svc;
}

int64_t WaitForProcessExit(const zx::debuglog& log, const Options::ProgramInfo& entry,
                           const ChildContext& child) {
  printl(log, "Waiting for %.*s to exit...", static_cast<int>(entry.filename().size()),
         entry.filename().data());
  zx_status_t status = child.process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
  check(log, status, "zx_object_wait_one on process failed");
  zx_info_process_t info;
  status = child.process.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
  check(log, status, "zx_object_get_info on process failed");
  printl(log, "*** Exit status %zd ***\n", info.return_code);
  return info.return_code;
}

struct TerminationInfo {
  // Depending on test mode and result, this might be the return code of boot or test elf.
  std::optional<int64_t> test_return_code;

  // Whether we should continue or shutdown.
  bool should_shutdown = false;

  zx::resource power;
};

[[noreturn]] void HandleTermination(const zx::debuglog& log, const TerminationInfo& info) {
  if (!info.should_shutdown) {
    printl(log, "finished!");
    zx_process_exit(0);
  }

  // The test runners match this exact string on the console log
  // to determine that the test succeeded since shutting the
  // machine down doesn't return a value to anyone for us.
  if (info.test_return_code && info.test_return_code == 0) {
    printl(log, "%s\n", BOOT_TEST_SUCCESS_STRING);
  }

  printl(log, "Process exited.  Executing poweroff");
  zx_system_powerctl(info.power.get(), ZX_SYSTEM_POWERCTL_SHUTDOWN, nullptr);
  printl(log, "still here after poweroff!");

  while (true)
    __builtin_trap();

  __UNREACHABLE;
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

  zx::debuglog log;
  auto status = zx::debuglog::create(*zx::unowned_resource{handles[kRootResource]}, 0, &log);
  check(log, status, "zx_debuglog_create failed: %d", status);

  zx::vmar vmar_self{handles[kVmarRootSelf]};
  handles[kVmarRootSelf] = ZX_HANDLE_INVALID;

  zx::process proc_self{handles[kProcSelf]};
  handles[kProcSelf] = ZX_HANDLE_INVALID;

  auto [power, vmex] = CreateResources(log, handles);
  zx::channel svc_stash_server, svc_stash_client;
  status = zx::channel::create(0, &svc_stash_server, &svc_stash_client);
  check(log, status, "Failed to create svc stash channel.");

  // Locate the ZBI_TYPE_STORAGE_BOOTFS item and decompress it. This will be used to load
  // the binary referenced by userboot.next, as well as libc. Bootfs will be fully parsed
  // and hosted under '/boot' either by bootsvc or component manager.
  const zx::unowned_vmo zbi{handles[kZbi]};
  zx::vmo bootfs_vmo = GetBootfsFromZbi(log, vmar_self, *zbi);

  // Parse CMDLINE items to determine the set of runtime options.
  Options opts = GetOptionsFromZbi(log, vmar_self, *zbi);

  TerminationInfo info = {
      .power = std::move(power),
  };

  {
    auto borrowed_bootfs = bootfs_vmo.borrow();
    Bootfs bootfs{vmar_self.borrow(), std::move(bootfs_vmo), std::move(vmex), DuplicateOrDie(log)};
    auto launch_process = [&](auto& elf_entry,
                              zx::channel svc_stash = zx::channel()) -> ChildContext {
      ChildMessageLayout child_message = CreateChildMessage();
      ChildContext child = CreateChildContext(log, elf_entry.filename(), handles);
      size_t handle_count = kChildHandleCount - 1;

      StashSvc(log, svc_stash_client, elf_entry.filename(), std::move(child.svc_server));
      SetChildHandles(log, *borrowed_bootfs, child);
      if (svc_stash) {
        SetStashHandle(log, std::move(svc_stash), child.handles);
        handle_count++;
      }

      // Fill in any '+' separated arguments provided by `userboot.next`. If arguments are longer
      // than kProcessArgsMaxBytes, this function will fail process creation.
      ParseNextProcessArguments(log, elf_entry.next, child_message.header.args_num,
                                child_message.args.data());

      // Map in the bootfs so we can look for files in it.
      zx::channel loader_svc =
          StartChildProcess(log, elf_entry, child_message, child, bootfs, handle_count);
      printl(log, "process %.*s started.", static_cast<int>(elf_entry.filename().size()),
             elf_entry.filename().data());

      // Now become the loader service for as long as that's needed.
      if (loader_svc) {
        LoaderService ldsvc(DuplicateOrDie(log), &bootfs, elf_entry.root);
        ldsvc.Serve(std::move(loader_svc));
      }

      return child;
    };

    if (!opts.test.next.empty()) {
      // If no boot, then hand over the stash to the test program. Test does not get the svc stash.
      auto test_context = launch_process(opts.test);
      // Wait for test to finish.
      info.test_return_code = WaitForProcessExit(log, opts.test, test_context);

      info.should_shutdown = opts.boot.next.empty();
    }

    if (!opts.boot.next.empty()) {
      [[maybe_unused]] auto boot_context = launch_process(opts.boot, std::move(svc_stash_server));
    }
  }
  HandleTermination(log, info);
}

}  // anonymous namespace

// This is the entry point for the whole show, the very first bit of code
// to run in user mode.
extern "C" [[noreturn]] void _start(zx_handle_t arg) { Bootstrap(zx::channel{arg}); }
