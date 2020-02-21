// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/elf-psabi/sp.h>
#include <lib/processargs/processargs.h>
#include <lib/userabi/userboot.h>
#include <lib/zircon-internal/default_stack_size.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <sys/param.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/system.h>

#include <array>
#include <climits>
#include <cstring>

#include "bootdata.h"
#include "bootfs.h"
#include "loader-service.h"
#include "option.h"
#include "userboot-elf.h"
#include "util.h"

namespace {

constexpr const char kStackVmoName[] = "userboot-child-initial-stack";

using namespace userboot;

[[noreturn]] void do_powerctl(zx_handle_t log, zx_handle_t rroot, uint32_t reason) {
  const char* r_str = (reason == ZX_SYSTEM_POWERCTL_SHUTDOWN) ? "poweroff" : "reboot";
  if (reason == ZX_SYSTEM_POWERCTL_REBOOT) {
    printl(log, "Waiting 3 seconds...");
    zx_nanosleep(zx_deadline_after(ZX_SEC(3u)));
  }

  printl(log, "Process exited.  Executing \"%s\".", r_str);
  zx_system_powerctl(rroot, reason, NULL);
  printl(log, "still here after %s!", r_str);
  while (true)
    __builtin_trap();
}

void load_child_process(zx_handle_t log, const struct options* o, struct bootfs* bootfs,
                        const char* root_prefix, zx_handle_t vdso_vmo, zx_handle_t proc,
                        zx_handle_t vmar, zx_handle_t thread, zx_handle_t to_child,
                        zx_vaddr_t* entry, zx_vaddr_t* vdso_base, size_t* stack_size,
                        zx_handle_t* loader_svc) {
  // Examine the bootfs image and find the requested file in it.
  // This will handle a PT_INTERP by doing a second lookup in bootfs.
  *entry = elf_load_bootfs(log, bootfs, root_prefix, proc, vmar, thread, o->value[OPTION_FILENAME],
                           to_child, stack_size, loader_svc);

  // Now load the vDSO into the child, so it has access to system calls.
  *vdso_base = elf_load_vmo(log, vmar, vdso_vmo);
}

// Reserve roughly the low half of the address space, so the initial
// process can use sanitizers that need to allocate shadow memory there.
// The reservation VMAR is kept around just long enough to make sure all
// the initial allocations (mapping in the initial ELF object, and
// allocating the initial stack) stay out of this area, and then destroyed.
// The process's own allocations can then use the full address space; if
// it's using a sanitizer, it will set up its shadow memory first thing.
zx_handle_t reserve_low_address_space(zx_handle_t log, zx_handle_t root_vmar) {
  zx_info_vmar_t info;
  check(log, zx_object_get_info(root_vmar, ZX_INFO_VMAR, &info, sizeof(info), NULL, NULL),
        "zx_object_get_info failed on child root VMAR handle");
  zx_handle_t vmar;
  uintptr_t addr;
  size_t reserve_size = (((info.base + info.len) / 2) + PAGE_SIZE - 1) & -PAGE_SIZE;
  zx_status_t status =
      zx_vmar_allocate(root_vmar, ZX_VM_SPECIFIC, 0, reserve_size - info.base, &vmar, &addr);
  check(log, status, "zx_vmar_allocate failed for low address space reservation");
  if (addr != info.base)
    fail(log, "zx_vmar_allocate gave wrong address?!?");
  return vmar;
}

// This is the main logic:
// 1. Read the kernel's bootstrap message.
// 2. Load up the child process from ELF file(s) on the bootfs.
// 3. Create the initial thread and allocate a stack for it.
// 4. Load up a channel with the zx_proc_args_t message for the child.
// 5. Start the child process running.
// 6. Optionally, wait for it to exit and then shut down.
[[noreturn]] void bootstrap(zx::channel channel) {
  // Before we've gotten the root resource and created the debuglog,
  // errors will be reported via zx_debug_write.
  zx::debuglog log;

  // We don't need our own thread handle, but the child does.
  // We pass the decompressed BOOTFS VMO along as well as the others.
  // So we're passing along two more handles than we got.
  constexpr uint32_t kThreadSelf = kHandleCount;
  constexpr uint32_t kBootfsVmo = kHandleCount + 1;
  constexpr uint32_t kChildHandleCount = kHandleCount + 2;

  // This is the processargs message the child will receive.  The command
  // line block the kernel sends us is formatted exactly to become the
  // environ strings for the child message, so we read it directly into
  // the same buffer.
  struct child_message_layout {
    zx_proc_args_t pargs{};
    uint32_t info[kChildHandleCount];
    char cmdline[kCmdlineMax];  // aka environ
  } child_message;

  // We pass all the same handles the kernel gives us along to the child,
  // except replacing our own process/root-VMAR handles with its, and
  // passing along the two extra handles (BOOTFS and thread-self).
  std::array<zx_handle_t, kChildHandleCount> handles;

  // Read the command line and the essential handles from the kernel.
  uint32_t cmdline_len, nhandles;
  zx_status_t status =
      channel.read(0, child_message.cmdline, handles.data(), sizeof(child_message.cmdline),
                   handles.size(), &cmdline_len, &nhandles);
  check(log.get(), status, "cannot read bootstrap message");
  if (nhandles != kHandleCount) {
    fail(log.get(), "read %u handles instead of %u", nhandles, kHandleCount);
  }

  // All done with the channel from the kernel now.  Let it go.
  channel.reset();

  // Now that we have the root resource, we can use it to get a debuglog.
  status = zx::debuglog::create(*zx::unowned_resource{handles[kRootResource]}, 0, &log);
  if (status != ZX_OK || !log) {
    printl(ZX_HANDLE_INVALID, "zx_debuglog_create failed: %d, using zx_debug_write instead",
           status);
  }

  // Hang on to the resource root handle in case we call powerctl below.
  zx::resource root_resource;
  status = zx::unowned_resource { handles[kRootResource] }
  ->duplicate(ZX_RIGHT_SAME_RIGHTS, &root_resource);
  check(log.get(), status, "zx_handle_duplicate");

  // Process the kernel command line, which gives us options and also
  // becomes the environment strings for our child.
  options o;
  child_message.pargs.environ_num =
      parse_options(log.get(), child_message.cmdline, cmdline_len, &o);

  // Fill in the child message header.
  child_message.pargs.protocol = ZX_PROCARGS_PROTOCOL;
  child_message.pargs.version = ZX_PROCARGS_VERSION;
  child_message.pargs.handle_info_off = offsetof(child_message_layout, info);
  child_message.pargs.environ_off = offsetof(child_message_layout, cmdline);

  // Fill in the handle info table.
  child_message.info[kBootfsVmo] = PA_HND(PA_VMO_BOOTFS, 0);
  child_message.info[kProcSelf] = PA_HND(PA_PROC_SELF, 0);
  child_message.info[kRootJob] = PA_HND(PA_JOB_DEFAULT, 0);
  child_message.info[kRootResource] = PA_HND(PA_RESOURCE, 0);
  child_message.info[kThreadSelf] = PA_HND(PA_THREAD_SELF, 0);
  child_message.info[kVmarRootSelf] = PA_HND(PA_VMAR_ROOT, 0);
  child_message.info[kZbi] = PA_HND(PA_VMO_BOOTDATA, 0);
  for (uint32_t i = kFirstVdso; i <= kLastVdso; ++i) {
    child_message.info[i] = PA_HND(PA_VMO_VDSO, i - kFirstVdso);
  }
  for (uint32_t i = kFirstKernelFile; i < kHandleCount; ++i) {
    child_message.info[i] = PA_HND(PA_VMO_KERNEL_FILE, i - kFirstKernelFile);
  }

  // Hang on to our own process handle.  If we closed it, our process
  // would be killed.  Exiting will clean it up.
  zx::process proc_self{handles[kProcSelf]};
  handles[kProcSelf] = ZX_HANDLE_INVALID;

  // We need our own root VMAR handle to map in the ZBI.
  zx::vmar vmar_self{handles[kVmarRootSelf]};
  handles[kVmarRootSelf] = ZX_HANDLE_INVALID;

  // Locate the first bootfs bootdata section and decompress it.
  // We need it to load devmgr and libc from.
  // Later bootfs sections will be processed by devmgr.
  zx::vmo bootfs_vmo{bootdata_get_bootfs(log.get(), vmar_self.get(), handles[kRootJob],
                                         handles[kUserbootDecompressor], handles[kFirstVdso],
                                         handles[kZbi])};

  // Map in the bootfs so we can look for files in it.
  struct bootfs bootfs;
  bootfs_mount(vmar_self.get(), log.get(), bootfs_vmo.get(), &bootfs);

  // Pass the decompressed bootfs VMO on.
  handles[kBootfsVmo] = bootfs_vmo.release();

  // Canonicalize the (possibly empty) userboot.root option string so that
  // it never starts with a slash but always ends with a slash unless it's
  // empty.  That lets us use it as a simple prefix on the full file name
  // strings in the BOOTFS directory.
  const char* root_option = o.value[OPTION_ROOT];
  while (*root_option == '/') {
    ++root_option;
  }
  size_t root_len = strlen(root_option);
  char root_prefix[NAME_MAX + 1];
  if (root_len >= sizeof(root_prefix) - 1) {
    fail(log.get(), "`userboot.root` string too long (%zu > %zu)", root_len,
         sizeof(root_prefix) - 1);
  }
  memcpy(root_prefix, root_option, root_len);
  while (root_len > 0 && root_prefix[root_len - 1] == '/') {
    --root_len;
  }
  if (root_len > 0) {
    root_prefix[root_len++] = '/';
  }
  root_prefix[root_len] = '\0';

  // Make the channel for the bootstrap message.
  zx::channel to_child, child_start_handle;
  status = zx::channel::create(0, &to_child, &child_start_handle);
  check(log.get(), status, "zx_channel_create failed");

  // Create the process itself.
  zx::process proc;
  zx::vmar vmar;
  zx::unowned_job root_job{handles[kRootJob]};
  const char* filename = o.value[OPTION_FILENAME];
  status = zx::process::create(*root_job, filename, static_cast<uint32_t>(strlen(filename)), 0,
                               &proc, &vmar);
  check(log.get(), status, "zx_process_create");

  // Squat on some address space before we start loading it up.
  zx::vmar reserve_vmar{reserve_low_address_space(log.get(), vmar.get())};

  // Create the initial thread in the new process
  zx::thread thread;
  status = zx::thread::create(proc, filename, static_cast<uint32_t>(strlen(filename)), 0, &thread);
  check(log.get(), status, "zx_thread_create");

  // Map in the code.
  zx_vaddr_t entry, vdso_base;
  size_t stack_size = ZIRCON_DEFAULT_STACK_SIZE;
  zx::channel loader_service_channel;
  load_child_process(log.get(), &o, &bootfs, root_prefix, handles[kFirstVdso], proc.get(),
                     vmar.get(), thread.get(), to_child.get(), &entry, &vdso_base, &stack_size,
                     loader_service_channel.reset_and_get_address());

  // Allocate the stack for the child.
  uintptr_t sp;
  {
    stack_size = (stack_size + PAGE_SIZE - 1) & -PAGE_SIZE;
    zx::vmo stack_vmo;
    status = zx::vmo::create(stack_size, 0, &stack_vmo);
    check(log.get(), status, "zx_vmo_create failed for child stack");
    stack_vmo.set_property(ZX_PROP_NAME, kStackVmoName, sizeof(kStackVmoName) - 1);
    zx_vaddr_t stack_base;
    status = vmar.map(0, stack_vmo, 0, stack_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &stack_base);
    check(log.get(), status, "zx_vmar_map failed for child stack");
    sp = compute_initial_stack_pointer(stack_base, stack_size);
  }

  // We're done doing mappings, so clear out the reservation VMAR.
  check(log.get(), reserve_vmar.destroy(), "zx_vmar_destroy failed on reservation VMAR handle");
  reserve_vmar.reset();

  // Pass along the child's root VMAR.  We're done with it.
  handles[kVmarRootSelf] = vmar.release();

  // Duplicate the child's process handle to pass to it.
  status = zx_handle_duplicate(proc.get(), ZX_RIGHT_SAME_RIGHTS, &handles[kProcSelf]);
  check(log.get(), status, "zx_handle_duplicate failed on child process handle");

  // Duplicate the child's thread handle to pass to it.
  status = zx_handle_duplicate(thread.get(), ZX_RIGHT_SAME_RIGHTS, &handles[kThreadSelf]);
  check(log.get(), status, "zx_handle_duplicate failed on child thread handle");

  for (const auto& h : handles) {
    zx_info_handle_basic_t info;
    status = zx_object_get_info(h, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    check(log.get(), status, "bad handle %d is %x", (int)(&h - &handles[0]), h);
  }

  // Now send the bootstrap message.  This transfers away all the handles
  // we have left except the process and thread themselves.
  status = to_child.write(0, &child_message, sizeof(child_message), handles.data(), handles.size());
  check(log.get(), status, "zx_channel_write to child failed");
  to_child.reset();

  // Start the process going.
  status = proc.start(thread, entry, sp, std::move(child_start_handle), vdso_base);
  check(log.get(), status, "zx_process_start failed");
  thread.reset();

  printl(log.get(), "process %s started.", o.value[OPTION_FILENAME]);

  // Now become the loader service for as long as that's needed.
  if (loader_service_channel) {
    LoaderService ldsvc(log.get(), &bootfs, root_prefix);
    ldsvc.Serve(std::move(loader_service_channel));
  }

  // All done with bootfs!
  bootfs_unmount(vmar_self.get(), log.get(), &bootfs);

  if (o.value[OPTION_SHUTDOWN] || o.value[OPTION_REBOOT]) {
    printl(log.get(), "Waiting for %s to exit...", o.value[OPTION_FILENAME]);
    status = proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
    check(log.get(), status, "zx_object_wait_one on process failed");
    zx_info_process_t info;
    status = proc.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    check(log.get(), status, "zx_object_get_info on process failed");
    printl(log.get(), "*** Exit status %zd ***\n", info.return_code);
    if (info.return_code == 0) {
      // The test runners match this exact string on the console log
      // to determine that the test succeeded since shutting the
      // machine down doesn't return a value to anyone for us.
      printl(log.get(), "%s\n", ZBI_TEST_SUCCESS_STRING);
    }
    if (o.value[OPTION_REBOOT]) {
      do_powerctl(log.get(), root_resource.get(), ZX_SYSTEM_POWERCTL_REBOOT);
    } else if (o.value[OPTION_SHUTDOWN]) {
      do_powerctl(log.get(), root_resource.get(), ZX_SYSTEM_POWERCTL_SHUTDOWN);
    }
  }

  // Now we've accomplished our purpose in life, and we can die happy.
  proc.reset();

  printl(log.get(), "finished!");
  zx_process_exit(0);
}

}  // anonymous namespace

// This is the entry point for the whole show, the very first bit of code
// to run in user mode.
extern "C" [[noreturn]] void _start(zx_handle_t arg) { bootstrap(zx::channel{arg}); }
