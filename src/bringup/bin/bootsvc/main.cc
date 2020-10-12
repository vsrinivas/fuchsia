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
#include <zircon/syscalls/resource.h>
#include <zircon/syscalls/system.h>

#include <iterator>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>

#include <fbl/string.h>
#include <fbl/vector.h>
#include <launchpad/launchpad.h>

#include "bootfs-loader-service.h"
#include "bootfs-service.h"
#include "svcfs-service.h"
#include "util.h"

namespace {

static constexpr const char kBootfsVmexName[] = "bootfs_vmex";

struct Resources {
  // TODO(smpham): Remove root resource.
  zx::resource root;
  zx::resource mmio;
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

// Parse ZBI_TYPE_IMAGE_ARGS item into boot args buffer
zx_status_t ExtractBootArgsFromImage(fbl::Vector<char>* buf, const zx::vmo& image_vmo,
                                     bootsvc::ItemMap* item_map) {
  auto it = item_map->find(bootsvc::ItemKey{ZBI_TYPE_IMAGE_ARGS, 0});
  if (it == item_map->end()) {
    return ZX_ERR_NOT_FOUND;
  }

  auto cfg = std::make_unique<char[]>(it->second.length);

  // read cfg data
  zx_status_t status = image_vmo.read(cfg.get(), it->second.offset, it->second.length);
  if (status != ZX_OK) {
    return status;
  }

  // Parse boot arguments file from bootdata.
  std::string_view str(cfg.get(), it->second.length);
  status = bootsvc::ParseBootArgs(std::move(str), buf);
  if (status != ZX_OK) {
    return status;
  }

  item_map->erase(it);
  return ZX_OK;
}

zx_status_t ExtractBootArgsFromBootfs(fbl::Vector<char>* buf,
                                      const fbl::RefPtr<bootsvc::BootfsService>& bootfs) {
  // TODO(teisenbe): Rename this file
  const char* config_path = "config/devmgr";

  // This config file may not be present depending on the device, but errors besides NOT_FOUND
  // should not be ignored.
  zx::vmo config_vmo;
  uint64_t file_size;
  zx_status_t status = bootfs->Open(config_path, /*executable=*/false, &config_vmo, &file_size);
  if (status == ZX_ERR_NOT_FOUND) {
    printf("bootsvc: No boot config found in bootfs, skipping\n");
    return ZX_OK;
  } else if (status != ZX_OK) {
    return status;
  }

  auto config = std::make_unique<char[]>(file_size);
  status = config_vmo.read(config.get(), 0, file_size);
  if (status != ZX_OK) {
    return status;
  }

  // Parse boot arguments file from bootfs.
  std::string_view str(config.get(), file_size);
  return bootsvc::ParseBootArgs(std::move(str), buf);
}

// Load the boot arguments from bootfs/ZBI_TYPE_IMAGE_ARGS and environment variables.
zx_status_t LoadBootArgs(const fbl::RefPtr<bootsvc::BootfsService>& bootfs,
                         const zx::vmo& image_vmo, bootsvc::ItemMap* item_map, zx::vmo* out,
                         uint64_t* size) {
  fbl::Vector<char> boot_args;
  zx_status_t status;

  status = ExtractBootArgsFromImage(&boot_args, image_vmo, item_map);
  ZX_ASSERT_MSG(((status == ZX_OK) || (status == ZX_ERR_NOT_FOUND)),
                "Retrieving boot args failed: %s\n", zx_status_get_string(status));

  status = ExtractBootArgsFromBootfs(&boot_args, bootfs);
  ZX_ASSERT_MSG(status == ZX_OK, "Retrieving boot config failed: %s\n",
                zx_status_get_string(status));

  // Add boot arguments from environment variables.
  for (char** e = environ; *e != nullptr; e++) {
    for (const char* x = *e; *x != 0; x++) {
      boot_args.push_back(*x);
    }
    boot_args.push_back(0);
  }

  // Copy boot arguments into VMO.
  zx::vmo args_vmo;
  status = zx::vmo::create(boot_args.size(), 0, &args_vmo);
  if (status != ZX_OK) {
    return status;
  }
  status = args_vmo.write(boot_args.data(), 0, boot_args.size());
  if (status != ZX_OK) {
    return status;
  }
  status = args_vmo.replace(ZX_DEFAULT_VMO_RIGHTS & ~ZX_RIGHT_WRITE, &args_vmo);
  if (status != ZX_OK) {
    return status;
  }
  *out = std::move(args_vmo);
  *size = boot_args.size();
  return ZX_OK;
}

// Launch the next process in the boot chain.
// It will receive:
// - stdout & stderr wired up to a debuglog handle
// - A namespace containing:
//   - A /boot directory, connected to the bootfs service hosted by bootsvc
//   - A /svc directory, containing other services hosted by bootsvc, including:
//     - fuchsia.boot.Arguments, which provides boot cmdline arguments
//     - fuchsia.boot.Items, which allows querying for certain ZBI items
//     - fuchsia.boot.{ReadOnly|WriteOnly}Log, which provides debuglog handles
//     - fuchsia.boot.RootResource, which provides root resource handles
//     - fuchsia.kernel.MmioResource, which provides mmio resource handles
//     - fuchsia.kernel.RootJob, which provides root job handles
// - A loader that can load libraries from /boot, hosted by bootsvc
//
// If the next process terminates, bootsvc will quit.
void LaunchNextProcess(fbl::RefPtr<bootsvc::BootfsService> bootfs,
                       fbl::RefPtr<bootsvc::SvcfsService> svcfs,
                       std::shared_ptr<bootsvc::BootfsLoaderService> loader_svc,
                       Resources& resources, const zx::debuglog& log, async::Loop& loop,
                       const zx::vmo& bootargs_vmo, const uint64_t bootargs_size) {
  const char* bootsvc_next = getenv("bootsvc.next");
  if (bootsvc_next == nullptr) {
    // Note that arguments are comma-delimited.
    bootsvc_next =
        "bin/component_manager,"
        "fuchsia-boot:///#meta/root.cm,"
        "--config,"
        "/boot/config/component_manager";
  }

  // Split the bootsvc.next value into 1 or more arguments using ',' as a
  // delimiter.
  printf("bootsvc: bootsvc.next = %s\n", bootsvc_next);
  fbl::Vector<fbl::String> next_args = bootsvc::SplitString(bootsvc_next, ',');

  // Open the executable we will start next
  zx::vmo program;
  uint64_t file_size;
  const char* next_program = next_args[0].c_str();
  zx_status_t status = bootfs->Open(next_program, /*executable=*/true, &program, &file_size);
  ZX_ASSERT_MSG(status == ZX_OK, "bootsvc: failed to open '%s': %s\n", next_program,
                zx_status_get_string(status));

  // Get the bootfs fuchsia.io.Node service channel that we will hand to the
  // next process in the boot chain.
  zx::channel bootfs_conn;
  status = bootfs->CreateRootConnection(&bootfs_conn);
  ZX_ASSERT_MSG(status == ZX_OK, "bootfs conn creation failed: %s\n", zx_status_get_string(status));

  zx::channel svcfs_conn;
  status = svcfs->CreateRootConnection(&svcfs_conn);
  ZX_ASSERT_MSG(status == ZX_OK, "svcfs conn creation failed: %s\n", zx_status_get_string(status));

  const char* nametable[2] = {};
  uint32_t count = 0;

  launchpad_t* lp;
  launchpad_create(0, next_program, &lp);
  {
    // Use the local loader service backed directly by the primary BOOTFS.
    zx::status<zx::channel> loader_conn = loader_svc->Connect();
    ZX_ASSERT_MSG(loader_conn.is_ok(), "failed to connect to BootfsLoaderService : %s\n",
                  loader_conn.status_string());
    zx_handle_t old = launchpad_use_loader_service(lp, loader_conn.value().release());
    ZX_ASSERT(old == ZX_HANDLE_INVALID);
  }
  launchpad_load_from_vmo(lp, program.release());
  launchpad_clone(lp, LP_CLONE_DEFAULT_JOB);

  launchpad_add_handle(lp, bootfs_conn.release(), PA_HND(PA_NS_DIR, count));
  nametable[count++] = "/boot";
  launchpad_add_handle(lp, svcfs_conn.release(), PA_HND(PA_NS_DIR, count));
  nametable[count++] = "/svc";

  // Pass on mmio resources to the next process.
  launchpad_add_handle(lp, resources.mmio.release(), PA_HND(PA_MMIO_RESOURCE, 0));

  // Duplicate the root resource to pass to the next process.
  zx::resource root_rsrc_dup;
  status = resources.root.duplicate(ZX_RIGHT_SAME_RIGHTS, &root_rsrc_dup);
  ZX_ASSERT_MSG(status == ZX_OK && root_rsrc_dup.is_valid(), "Failed to duplicate root resource");
  launchpad_add_handle(lp, root_rsrc_dup.release(), PA_HND(PA_RESOURCE, 0));

  int argc = static_cast<int>(next_args.size());
  const char* argv[argc];
  for (int i = 0; i < argc; ++i) {
    argv[i] = next_args[i].c_str();
  }
  launchpad_set_args(lp, argc, argv);

  ZX_ASSERT(count <= std::size(nametable));
  launchpad_set_nametable(lp, count, nametable);

  // Set up the environment table for launchpad.
  std::vector<char> env(bootargs_size);
  std::vector<char*> envp;
  status = bootargs_vmo.read(env.data(), 0, bootargs_size);
  ZX_ASSERT_MSG(status == ZX_OK, "failed to read bootargs from vmo: %s",
                zx_status_get_string(status));

  uint64_t last_env_start = 0;
  uint64_t i;
  for (i = 0; i < bootargs_size; i++) {
    if (env[i] == 0) {
      envp.push_back(&env[last_env_start]);
      last_env_start = i + 1;
    }
  }
  envp.push_back(nullptr);

  status = launchpad_set_environ(lp, envp.data());
  if (status != ZX_OK) {
    launchpad_abort(lp, status, "bootsvc: cannot set up environment");
  }

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

}  // namespace

int main(int argc, char** argv) {
  // Close the loader-service channel so the service can go away.
  // We won't use it any more (no dlopen calls in this process).
  zx_handle_close(dl_set_loader_service(ZX_HANDLE_INVALID));

  // NOTE: This will be the only source of zx::debuglog in the system.
  // Eventually, we will receive this through a startup handle from userboot.
  zx::debuglog log;
  zx_status_t status = zx::debuglog::create(zx::resource(), 0, &log);
  ZX_ASSERT(status == ZX_OK);

  status = SetupStdout(log);
  ZX_ASSERT(status == ZX_OK);

  printf("bootsvc: Starting...\n");

  status = zx::job::default_job()->set_critical(0, *zx::process::self());
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to set bootsvc as critical to root-job: %s\n",
                zx_status_get_string(status));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::vmo bootfs_vmo(zx_take_startup_handle(PA_HND(PA_VMO_BOOTFS, 0)));
  ZX_ASSERT(bootfs_vmo.is_valid());

  // Take the resources.
  printf("bootsvc: Taking resources...\n");
  Resources resources;
  resources.mmio.reset(zx_take_startup_handle(PA_HND(PA_MMIO_RESOURCE, 0)));
  ZX_ASSERT_MSG(resources.mmio.is_valid(), "Invalid MMIO root resource handle\n");
  resources.root.reset(zx_take_startup_handle(PA_HND(PA_RESOURCE, 0)));
  ZX_ASSERT_MSG(resources.root.is_valid(), "Invalid root resource handle\n");

  // Create a VMEX resource object to provide the bootfs service.
  // TODO(smpham): Pass VMEX resource from kernel.
  zx::resource bootfs_vmex_rsrc;
  status = zx::resource::create(resources.root, ZX_RSRC_KIND_VMEX, 0, 0, kBootfsVmexName,
                                sizeof(kBootfsVmexName), &bootfs_vmex_rsrc);
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to create VMEX resource");

  // Set up the bootfs service
  printf("bootsvc: Creating bootfs service...\n");
  fbl::RefPtr<bootsvc::BootfsService> bootfs_svc;
  status =
      bootsvc::BootfsService::Create(loop.dispatcher(), std::move(bootfs_vmex_rsrc), &bootfs_svc);
  ZX_ASSERT_MSG(status == ZX_OK, "BootfsService creation failed: %s\n",
                zx_status_get_string(status));
  status = bootfs_svc->AddBootfs(std::move(bootfs_vmo));
  ZX_ASSERT_MSG(status == ZX_OK, "Bootfs add failed: %s\n", zx_status_get_string(status));

  // Process the ZBI boot image
  printf("bootsvc: Retrieving boot image...\n");
  zx::vmo image_vmo;
  bootsvc::ItemMap item_map;
  bootsvc::FactoryItemMap factory_item_map;
  bootsvc::BootloaderFileMap bootloader_file_map;
  status =
      bootsvc::RetrieveBootImage(&image_vmo, &item_map, &factory_item_map, &bootloader_file_map);
  ZX_ASSERT_MSG(status == ZX_OK, "Retrieving boot image failed: %s\n",
                zx_status_get_string(status));

  // Load boot arguments into VMO
  printf("bootsvc: Loading boot arguments...\n");
  zx::vmo args_vmo;
  uint64_t args_size = 0;
  status = LoadBootArgs(bootfs_svc, image_vmo, &item_map, &args_vmo, &args_size);
  ZX_ASSERT_MSG(status == ZX_OK, "Loading boot arguments failed: %s\n",
                zx_status_get_string(status));

  // Set up the svcfs service
  printf("bootsvc: Creating svcfs service...\n");
  fbl::RefPtr<bootsvc::SvcfsService> svcfs_svc = bootsvc::SvcfsService::Create(loop.dispatcher());
  svcfs_svc->AddService(
      fuchsia_boot_Items_Name,
      bootsvc::CreateItemsService(loop.dispatcher(), std::move(image_vmo), std::move(item_map),
                                  std::move(bootloader_file_map)));
  svcfs_svc->AddService(
      fuchsia_boot_FactoryItems_Name,
      bootsvc::CreateFactoryItemsService(loop.dispatcher(), std::move(factory_item_map)));

  // Consume certain VMO types from the startup handle table
  printf("bootsvc: Loading kernel VMOs...\n");
  bootfs_svc->PublishStartupVmos(PA_VMO_VDSO, "PA_VMO_VDSO");
  bootfs_svc->PublishStartupVmos(PA_VMO_KERNEL_FILE, "PA_VMO_KERNEL_FILE");

  // Creating the loader service
  printf("bootsvc: Creating loader service...\n");
  auto loader_svc = bootsvc::BootfsLoaderService::Create(loop.dispatcher(), bootfs_svc);

  // Launch the next process in the chain.  This must be in a thread, since
  // it may issue requests to the loader, which runs in the async loop that
  // starts running after this.
  printf("bootsvc: Launching next process...\n");
  std::thread(LaunchNextProcess, bootfs_svc, svcfs_svc, loader_svc, std::ref(resources),
              std::cref(log), std::ref(loop), std::cref(args_vmo), args_size)
      .detach();

  // Begin serving the bootfs fileystem and loader
  loop.Run();
  printf("bootsvc: Exiting\n");
  return 0;
}
