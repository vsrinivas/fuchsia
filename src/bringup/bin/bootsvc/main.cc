// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <fuchsia/boot/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/boot/image.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls/clock.h>
#include <zircon/syscalls/resource.h>
#include <zircon/syscalls/system.h>
#include <zircon/utc.h>

#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <launchpad/launchpad.h>

#include "bootfs-loader-service.h"
#include "bootfs-service.h"
#include "util.h"

namespace {

static constexpr const char kBootfsVmexName[] = "bootfs_vmex";

constexpr std::string_view kBootsvcNextDefault =
    "bin/component_manager,"
    "fuchsia-boot:///#meta/root.cm,"
    "--config,"
    "/boot/config/component_manager";

struct Resources {
  // TODO(smpham): Remove root resource.
  zx::resource root;
  zx::resource mmio;
  zx::resource irq;
  zx::resource system;
  zx::resource vmex;
#if __x86_64__
  zx::resource ioport;
#elif __aarch64__
  zx::resource smc;
#endif
};

// This is a temporary hack used while bootsvc and component manager
// are both creating bootfs services, as VMO handles taken need to be
// passed on.
struct Vmos {
  zx::vmo zbi;
  zx::vmo bootfs;
  zx::vmo bootfs_exec;
  std::vector<zx::vmo> vdsos;
  std::vector<zx::vmo> kernel_files;
};

// Wire up stdout so that printf() and friends work.
zx_status_t SetupStdout(const zx::debuglog& log) {
  zx::debuglog dup;
  zx_status_t status = log.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  if (status != ZX_OK) {
    return status;
  }
  fdio_t* logger = nullptr;
  status = fdio_create(dup.release(), &logger);
  if (status != ZX_OK) {
    return status;
  }
  int fd = fdio_bind_to_fd(logger, 1, 0);
  if (fd != 1) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

// Create and install a userspace UTC clock that will remain fixed at zero.
zx_status_t InitializeClock() {
  zx_clock_create_args_v1_t args = {};
  zx_handle_t clock, prev_clock;
  zx_status_t status = zx_clock_create(ZX_CLOCK_ARGS_VERSION(1), &args, &clock);
  if (status != ZX_OK) {
    return status;
  }
  status = zx_utc_reference_swap(clock, &prev_clock);
  if (status != ZX_OK) {
    return status;
  }
  if (prev_clock != ZX_HANDLE_INVALID) {
    printf("bootsvc: UTC clock was not empty during initialization\n");
    zx_handle_close(prev_clock);
  }
  return ZX_OK;
}

// Parse ZBI_TYPE_IMAGE_ARGS-encoded arguments out of the given ZBI.
zx_status_t ExtractBootArgsFromImage(const zx::vmo& image_vmo, bootsvc::ItemMap* item_map,
                                     std::optional<std::string>* next_program) {
  auto it = item_map->find(bootsvc::ItemKey{ZBI_TYPE_CMDLINE, 0});
  if (it == item_map->end()) {
    return ZX_OK;
  }

  for (const bootsvc::ItemValue& value : it->second) {
    auto [offset, length] = value;

    auto payload = std::make_unique<char[]>(length);
    if (zx_status_t status = image_vmo.read(payload.get(), offset, length); status != ZX_OK) {
      return status;
    }

    std::string_view str(payload.get(), length);
    if (auto next = bootsvc::GetBootsvcNext(str); next) {
      *next_program = next;
    }
  }

  item_map->erase(it);
  return ZX_OK;
}

// Launch the next process in the boot chain.
// It will receive:
// - stdout & stderr wired up to a debuglog handle
// - A namespace containing:
//   - (Optionally) A /boot directory, connected to the bootfs service hosted by bootsvc. If
//       --host_bootfs is passed to component manager, it will host bootfs instead.
// - A loader that can load libraries from /boot, hosted by bootsvc
//
// If the next process terminates, bootsvc will quit.
void LaunchNextProcess(const std::vector<std::string>& args,
                       fbl::RefPtr<bootsvc::BootfsService> bootfs,
                       std::shared_ptr<bootsvc::BootfsLoaderService> loader_svc,
                       Resources& resources, Vmos& vmos, const zx::debuglog& log,
                       async::Loop& loop) {
  ZX_DEBUG_ASSERT(!args.empty());

  // The nametable will contain bootfs only if --host_bootfs is not used.
  const char* nametable[1] = {};
  uint32_t count = 0;

  zx_status_t status;
  const char* next_program = args[0].c_str();

  launchpad_t* lp;
  launchpad_create(0, next_program, &lp);

  // Use the local loader service backed by the bootfs VFS or by a temporary bootfs
  // image parser if the bootfs VFS is no longer created in bootsvc.
  {
    auto loader_conn = loader_svc->Connect();
    ZX_ASSERT_MSG(loader_conn.is_ok(), "failed to connect to BootfsLoaderService : %s\n",
                  loader_conn.status_string());
    zx_handle_t old = launchpad_use_loader_service(lp, loader_conn->TakeChannel().release());
    ZX_ASSERT(old == ZX_HANDLE_INVALID);
  }

  if (bootfs) {
    zx::vmo program;
    uint64_t file_size;
    status = bootfs->Open(next_program, /*executable=*/true, &program, &file_size);
    ZX_ASSERT_MSG(status == ZX_OK, "bootsvc: failed to open '%s': %s\n", next_program,
                  zx_status_get_string(status));
    // Get the bootfs fuchsia.io.Node service channel that we will hand to the
    // next process in the boot chain.
    zx::channel bootfs_conn;
    status = bootfs->CreateRootConnection(&bootfs_conn);
    ZX_ASSERT_MSG(status == ZX_OK, "bootfs conn creation failed: %s\n",
                  zx_status_get_string(status));

    launchpad_load_from_vmo(lp, program.release());
    launchpad_clone(lp, LP_CLONE_DEFAULT_JOB);

    launchpad_add_handle(lp, bootfs_conn.release(), PA_HND(PA_NS_DIR, count));
    nametable[count++] = "/boot";
  } else {
    // Only the executable bootfs vmo is needed since we need an executable next program image.
    zbitl::MapUnownedVmo invalid_unowned_boofs, unowned_bootfs_exec(vmos.bootfs_exec.borrow());

    zx::status<zx::vmo> result = bootsvc::BootfsService::GetFileFromBootfsVmo(
        std::move(invalid_unowned_boofs), std::move(unowned_bootfs_exec), next_program,
        /*size=*/nullptr);
    ZX_ASSERT_MSG(result.status_value() == ZX_OK, "bootsvc: failed to open '%s': %s\n",
                  next_program, zx_status_get_string(result.status_value()));

    launchpad_load_from_vmo(lp, result.value().release());
    launchpad_clone(lp, LP_CLONE_DEFAULT_JOB);

    // Component manager only needs these handles if it's creating bootfs.
    for (uint16_t i = 0; i < vmos.vdsos.size(); i++) {
      // Don't overwrite the default VDSO at index i == 0.
      launchpad_add_handle(lp, vmos.vdsos[i].release(), PA_HND(PA_VMO_VDSO, i + 1));
    }
    for (uint16_t i = 0; i < vmos.kernel_files.size(); i++) {
      launchpad_add_handle(lp, vmos.kernel_files[i].release(), PA_HND(PA_VMO_KERNEL_FILE, i));
    }
  }

  // Pass on resources to the next process.
  launchpad_add_handle(lp, vmos.zbi.release(), PA_HND(PA_VMO_BOOTDATA, 0));
  launchpad_add_handle(lp, vmos.bootfs.release(), PA_HND(PA_VMO_BOOTFS, 0));
  launchpad_add_handle(lp, resources.mmio.release(), PA_HND(PA_MMIO_RESOURCE, 0));
  launchpad_add_handle(lp, resources.irq.release(), PA_HND(PA_IRQ_RESOURCE, 0));
  launchpad_add_handle(lp, resources.system.release(), PA_HND(PA_SYSTEM_RESOURCE, 0));
#if __x86_64__
  launchpad_add_handle(lp, resources.ioport.release(), PA_HND(PA_IOPORT_RESOURCE, 0));
#elif __aarch64__
  launchpad_add_handle(lp, resources.smc.release(), PA_HND(PA_SMC_RESOURCE, 0));
#endif

  // Duplicate the root resource to pass to the next process.
  zx::resource root_rsrc_dup;
  status = resources.root.duplicate(ZX_RIGHT_SAME_RIGHTS, &root_rsrc_dup);
  ZX_ASSERT_MSG(status == ZX_OK && root_rsrc_dup.is_valid(), "Failed to duplicate root resource");
  launchpad_add_handle(lp, root_rsrc_dup.release(), PA_HND(PA_RESOURCE, 0));

  auto argv = std::make_unique<const char*[]>(args.size());
  for (size_t i = 0; i < args.size(); ++i) {
    argv[i] = args[i].c_str();
  }
  launchpad_set_args(lp, static_cast<int>(args.size()), argv.get());

  ZX_ASSERT(count <= std::size(nametable));
  launchpad_set_nametable(lp, count, nametable);

  zx::debuglog log_dup;
  status = log.duplicate(ZX_RIGHT_SAME_RIGHTS, &log_dup);
  if (status != ZX_OK) {
    launchpad_abort(lp, status, "bootsvc: cannot duplicate debuglog handle");
  } else {
    launchpad_add_handle(lp, log_dup.release(), PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO));
  }

  const char* errmsg;
  zx::process proc_handle;
  status = launchpad_go(lp, proc_handle.reset_and_get_address(), &errmsg);
  if (status != ZX_OK) {
    printf("bootsvc: launchpad %s failed: %s: %s\n", next_program, errmsg,
           zx_status_get_string(status));
    return;
  }
  printf("bootsvc: Launched %s\n", next_program);

  // wait for termination and then reboot or power off the system
  zx_signals_t observed;
  zx_status_t termination_result =
      proc_handle.wait_one(ZX_TASK_TERMINATED, zx::time::basic_time::infinite(), &observed);
  if (termination_result != ZX_OK) {
    printf("bootsvc: failure waiting for next process termination %i\n", termination_result);
  }

  // If the next process terminated, quit the main loop.
  loop.Quit();
}

void GetKernelVmos(uint8_t type, std::vector<zx::vmo>* vmos) {
  zx::vmo vmo;
  for (uint16_t i = 0;; i++) {
    vmo = zx::vmo(zx_take_startup_handle(PA_HND(type, i)));
    if (vmo.is_valid()) {
      vmos->push_back(std::move(vmo));
    } else {
      return;
    }
  }
}

bool UseComponentManagerHostedBootfs(std::vector<std::string> next_args) {
  for (const std::string& arg : next_args) {
    if (arg.find("host_bootfs") != std::string::npos) {
      printf("bootsvc: using component manager hosted bootfs filesystem.\n");
      return true;
    }
  }

  printf(
      "bootsvc: using the legacy bootfs filesystem. To try the new component manager hosted "
      "filesystem, pass --host_bootfs as a bootsvc.next argument.\n");
  return false;
}

fbl::RefPtr<bootsvc::BootfsService> CreateBootfsService(async_dispatcher_t* dispatcher,
                                                        const Resources& resources,
                                                        const Vmos& vmos) {
  zx::resource vmex_dup;
  zx_status_t status = resources.vmex.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmex_dup);
  ZX_ASSERT_MSG(status == ZX_OK && vmex_dup.is_valid(),
                "Failed to duplicate bootfs_vmex resource: %s\n.", zx_status_get_string(status));

  fbl::RefPtr<bootsvc::BootfsService> bootfs_svc;
  status = bootsvc::BootfsService::Create(dispatcher, std::move(vmex_dup), &bootfs_svc);
  ZX_ASSERT_MSG(status == ZX_OK, "BootfsService creation failed: %s\n",
                zx_status_get_string(status));

  zx::vmo bootfs_dup;
  status = vmos.bootfs.duplicate(ZX_RIGHT_SAME_RIGHTS, &bootfs_dup);
  ZX_ASSERT_MSG(status == ZX_OK && bootfs_dup.is_valid(), "Failed to duplicate bootfs vmo: %s\n.",
                zx_status_get_string(status));
  ZX_ASSERT(bootfs_dup.is_valid());

  status = bootfs_svc->AddBootfs(std::move(bootfs_dup));
  ZX_ASSERT_MSG(status == ZX_OK, "Bootfs add failed: %s\n", zx_status_get_string(status));

  printf("bootsvc: Loading kernel VMOs...\n");
  bootfs_svc->PublishStartupVmos(vmos.vdsos, PA_VMO_VDSO, "PA_VMO_VDSO");
  bootfs_svc->PublishStartupVmos(vmos.kernel_files, PA_VMO_KERNEL_FILE, "PA_VMO_KERNEL_FILE");

  return bootfs_svc;
}

}  // namespace

int main(int argc, char** argv) {
  // Close the loader-service channel so the service can go away.
  // We won't use it any more (no dlopen calls in this process).
  zx_handle_close(dl_set_loader_service(ZX_HANDLE_INVALID));

  // NOTE: This will be the only source of zx::debuglog in the system.
  // Eventually, we will receive this through a startup handle from userboot.
  zx::resource root_resource(zx_take_startup_handle(PA_HND(PA_RESOURCE, 0)));
  ZX_ASSERT(root_resource.is_valid());
  zx::debuglog log;
  zx_status_t status = zx::debuglog::create(root_resource, 0, &log);
  ZX_ASSERT(status == ZX_OK);

  status = SetupStdout(log);
  ZX_ASSERT(status == ZX_OK);

  printf("bootsvc: Starting...\n");

  status = zx::job::default_job()->set_critical(0, *zx::process::self());
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to set bootsvc as critical to root-job: %s\n",
                zx_status_get_string(status));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  // Take the resources.
  printf("bootsvc: Taking resources...\n");
  Resources resources;
  resources.mmio.reset(zx_take_startup_handle(PA_HND(PA_MMIO_RESOURCE, 0)));
  ZX_ASSERT_MSG(resources.mmio.is_valid(), "Invalid MMIO root resource handle\n");
  resources.irq.reset(zx_take_startup_handle(PA_HND(PA_IRQ_RESOURCE, 0)));
  ZX_ASSERT_MSG(resources.irq.is_valid(), "Invalid IRQ root resource handle\n");
  resources.system.reset(zx_take_startup_handle(PA_HND(PA_SYSTEM_RESOURCE, 0)));
  ZX_ASSERT_MSG(resources.system.is_valid(), "Invalid system root resource handle\n");
#if __x86_64__
  resources.ioport.reset(zx_take_startup_handle(PA_HND(PA_IOPORT_RESOURCE, 0)));
  ZX_ASSERT_MSG(resources.ioport.is_valid(), "Invalid IOPORT root resource handle\n");
#elif __aarch64__
  resources.smc.reset(zx_take_startup_handle(PA_HND(PA_SMC_RESOURCE, 0)));
  ZX_ASSERT_MSG(resources.smc.is_valid(), "Invalid SMC root resource handle\n");
#endif
  resources.root = std::move(root_resource);
  ZX_ASSERT_MSG(resources.root.is_valid(), "Invalid root resource handle\n");

  // Create a vmex resource for marking bootfs VMO regions as executable.
  status = zx::resource::create(resources.system, ZX_RSRC_KIND_SYSTEM, ZX_RSRC_SYSTEM_VMEX_BASE, 1,
                                kBootfsVmexName, sizeof(kBootfsVmexName), &resources.vmex);
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to create VMEX resource");

  Vmos vmos;
  vmos.bootfs = zx::vmo(zx_take_startup_handle(PA_HND(PA_VMO_BOOTFS, 0)));
  ZX_ASSERT_MSG(vmos.bootfs.is_valid(), "Invalid bootfs vmo\n");

  zx::vmo vmo;
  status = vmos.bootfs.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  ZX_ASSERT_MSG(status == ZX_OK && vmo.is_valid(), "Failed to duplicate bootfs vmo: %s\n.",
                zx_status_get_string(status));
  status = vmo.replace_as_executable(resources.vmex, &vmo);
  ZX_ASSERT_MSG(status == ZX_OK && vmo.is_valid(), "Failed to mark bootfs vmo as executable: %s",
                zx_status_get_string(status));
  vmos.bootfs_exec = std::move(vmo);

  vmos.zbi = zx::vmo(zx_take_startup_handle(PA_HND(PA_VMO_BOOTDATA, 0)));
  ZX_ASSERT_MSG(vmos.zbi.is_valid(), "Invalid ZBI vmo\n");

  GetKernelVmos(PA_VMO_VDSO, &vmos.vdsos);
  GetKernelVmos(PA_VMO_KERNEL_FILE, &vmos.kernel_files);

  // We've taken the default VDSO handle (which will be published under /boot/kernel),
  // so duplicate it and add it back for launchpad to find.
  zx::vmo default_vdso;
  status = vmos.vdsos[0].duplicate(ZX_RIGHT_SAME_RIGHTS, &default_vdso);
  ZX_ASSERT_MSG(status == ZX_OK && default_vdso.is_valid(),
                "Failed to duplicate default vdso handle: %s\n.", zx_status_get_string(status));
  launchpad_set_vdso_vmo(default_vdso.release());

  // Memfs attempts to read the UTC clock to set file times, but bootsvc starts
  // before component_manager has created the standard UTC clock. Create a
  // process-local clock fixed to zero to avoid clock read errors.
  status = InitializeClock();
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to create clock: %s\n", zx_status_get_string(status));

  // Process the ZBI boot image
  printf("bootsvc: Retrieving boot image...\n");
  zx::vmo zbi_dup;
  status = vmos.zbi.duplicate(ZX_RIGHT_SAME_RIGHTS, &zbi_dup);
  ZX_ASSERT_MSG(status == ZX_OK && zbi_dup.is_valid(), "Failed to duplicate ZBI vmo: %s\n.",
                zx_status_get_string(status));

  zx::vmo image_vmo;
  bootsvc::ItemMap item_map;
  bootsvc::BootloaderFileMap bootloader_file_map;
  status =
      bootsvc::RetrieveBootImage(std::move(zbi_dup), &image_vmo, &item_map, &bootloader_file_map);
  ZX_ASSERT_MSG(status == ZX_OK, "Retrieving boot image failed: %s\n",
                zx_status_get_string(status));

  std::optional<std::string> next_program;
  printf("bootsvc: Loading CMDLINE boot arguments...\n");
  status = ExtractBootArgsFromImage(image_vmo, &item_map, &next_program);
  ZX_ASSERT_MSG(status == ZX_OK, "Retrieving CMDLINE boot args failed: %s\n",
                zx_status_get_string(status));
  if (!next_program) {
    next_program = kBootsvcNextDefault;
  }
  printf("bootsvc: bootsvc.next = %s\n", next_program->c_str());
  std::vector<std::string> next_args = bootsvc::SplitString(*next_program, ',');

  fbl::RefPtr<bootsvc::BootfsService> bootfs_svc = nullptr;
  std::shared_ptr<bootsvc::BootfsLoaderService> loader_svc = nullptr;

  bool create_bootfs = !UseComponentManagerHostedBootfs(next_args);
  if (create_bootfs) {
    printf("bootsvc: Creating bootfs service...\n");
    bootfs_svc = CreateBootfsService(loop.dispatcher(), resources, vmos);

    loader_svc = bootsvc::BootfsLoaderService::Create(loop.dispatcher(), bootfs_svc);
    ZX_ASSERT_MSG(loader_svc != nullptr, "Failed to create bootfs loader service\n");
  } else {
    // Duplicates the bootfs VMOs as needed.
    loader_svc =
        bootsvc::BootfsLoaderService::Create(loop.dispatcher(), vmos.bootfs, vmos.bootfs_exec);
    ZX_ASSERT_MSG(loader_svc != nullptr, "Failed to create bootfs loader service\n");
  }

  // Launch the next process in the chain.  This must be in a thread, since
  // it may issue requests to the loader, which runs in the async loop that
  // starts running after this.
  printf("bootsvc: Launching next process...\n");
  std::thread(LaunchNextProcess, next_args, bootfs_svc, loader_svc, std::ref(resources),
              std::ref(vmos), std::cref(log), std::ref(loop))
      .detach();

  // Begin serving the bootfs fileystem and loader
  loop.Run();
  printf("bootsvc: Exiting\n");
  return 0;
}
