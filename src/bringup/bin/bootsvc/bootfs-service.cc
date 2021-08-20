// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootfs-service.h"

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/items/bootfs.h>
#include <lib/zbitl/vmo.h>
#include <lib/zx/event.h>
#include <lib/zx/time.h>
#include <sys/stat.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <string_view>
#include <utility>

#include <fbl/algorithm.h>
#include <launchpad/launchpad.h>

#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "util.h"

namespace fio = fuchsia_io;

namespace bootsvc {

namespace {

using BootfsView = zbitl::BootfsView<zbitl::MapUnownedVmo>;

// 'Packages' in bootfs can contain executable files but we need to account for the package name
// path component, which can be anything. For example, 'pkg/my_package/bin' should be executable but
// 'pkg/my_package/foo' should not.
static constexpr std::string_view kBootfsPackagePrefix = "pkg/";
static constexpr std::string_view kExecutablePackageDirectories[] = {
    "bin/",
    "lib/",
};

static bool PathInExecutablePackageDirectory(std::string_view path) {
  // All packages in bootfs are located under a single directory.
  if (!cpp20::starts_with(path, kBootfsPackagePrefix)) {
    return false;
  }

  // Advance past the path separator separating the package name and path inside the package.
  path.remove_prefix(kBootfsPackagePrefix.size());
  std::string_view pkg_name = path.substr(0, path.find('/'));
  path.remove_prefix(std::min(pkg_name.size() + 1, path.size()));

  // Finally, check if the path inside the package is one of the allowlisted paths.
  for (std::string_view prefix : kExecutablePackageDirectories) {
    if (cpp20::starts_with(path, prefix)) {
      return true;
    }
  }
  return false;
}

// Other top-level directories in bootfs that are allowed to contain executable files (i.e. files
// for which bootfs should allow opening with OPEN_RIGHT_EXECUTABLE).
static constexpr std::string_view kExecutableDirectories[] = {
    "bin/",
    "driver/",
    "lib/",
    "test/",
};

static bool PathInExecutableDirectory(std::string_view path) {
  if (PathInExecutablePackageDirectory(path)) {
    return true;
  }

  for (std::string_view prefix : kExecutableDirectories) {
    if (cpp20::starts_with(path, prefix)) {
      return true;
    }
  }
  return false;
}

}  // namespace

BootfsService::BootfsService(zx::resource vmex_rsrc) : vmex_rsrc_(std::move(vmex_rsrc)) {}

zx_status_t BootfsService::Create(async_dispatcher_t* dispatcher, zx::resource vmex_rsrc,
                                  fbl::RefPtr<BootfsService>* out) {
  auto svc = fbl::AdoptRef(new BootfsService(std::move(vmex_rsrc)));

  zx_status_t status = memfs::Vfs::Create(dispatcher, "<root>", &svc->vfs_, &svc->root_);
  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(svc);
  return ZX_OK;
}

zx_status_t BootfsService::AddBootfs(zx::vmo bootfs_vmo) {
  // The bootfs VnodeVmo nodes are all created from the same backing VMO with differing offsets, and
  // memfs creates clones as needed. Executable files use a duplicate handle to this same VMO which
  // has ZX_RIGHT_EXECUTE added. This is done once here rather than in PublishUnownedVmo to avoid
  // lots of repetitive unnecessary syscalls for every executable file.
  zx::vmo bootfs_exec_vmo;
  zx_status_t status = DuplicateAsExecutable(bootfs_vmo, &bootfs_exec_vmo);
  if (status != ZX_OK) {
    return status;
  }

  // Load all of the entries in the bootfs into the FS.
  zbitl::MapUnownedVmo mapvmo{bootfs_vmo.borrow()};
  BootfsView bootfs;
  if (auto result = BootfsView::Create(std::move(mapvmo)); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return ZX_ERR_INTERNAL;
  } else {
    bootfs = std::move(result.value());
  }

  // A helper to have `status` encode the first error we come across: while it
  // is ZX_OK, it will be overridden with the next status; once it is an error
  // state, subsequent statuses will be ignored.
  auto update_status = [&status](zx_status_t next) {
    if (status == ZX_OK) {
      status = next;
    }
  };

  for (const auto& file : bootfs) {
    const zx::vmo& vmo = PathInExecutableDirectory(file.name) ? bootfs_exec_vmo : bootfs_vmo;
    update_status(PublishUnownedVmo(file.name, vmo, file.offset, file.size));
  }
  if (auto result = bootfs.take_error(); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    update_status(ZX_ERR_INTERNAL);
  }

  // Add these VMOs to our list of owned VMOs even on failure, since we may have
  // added a file.
  owned_vmos_.push_back(std::move(bootfs_vmo));
  owned_vmos_.push_back(std::move(bootfs_exec_vmo));

  return status;
}

zx_status_t BootfsService::CreateRootConnection(zx::channel* out) {
  return CreateVnodeConnection(vfs_.get(), root_, fs::Rights::ReadExec(), out);
}

zx_status_t BootfsService::Open(const char* path, bool executable, zx::vmo* vmo, size_t* size) {
  if (path != nullptr && (path[0] == '/' || path[0] == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto open_options =
      executable ? fs::VnodeConnectionOptions::ReadExec() : fs::VnodeConnectionOptions::ReadOnly();
  open_options.set_no_remote();

  // fdio cannot be used since it is synchronous, and the filesystem we're opening from is
  // in-process and single threaded, but using the ulib/fs APIs directly instead of going through
  // the fuchsia.io APIs risks behavior differences or skipped checks.
  auto open_result = vfs_->Open(root_, path, open_options, fs::Rights::ReadOnly(), 0);
  if (open_result.is_error()) {
    return open_result.error();
  }
  ZX_ASSERT(open_result.is_ok());
  fbl::RefPtr<fs::Vnode> node = std::move(open_result.ok().vnode);

  // memfs doesn't currently do anything different for VMO_FLAG_PRIVATE, but it may in the future,
  // and this matches the flags used by fdio_get_vmo_clone/exec.
  uint32_t vmo_flags = fio::wire::kVmoFlagRead | fio::wire::kVmoFlagPrivate;
  vmo_flags |= executable ? fio::wire::kVmoFlagExec : 0;
  return node->GetVmo(vmo_flags, vmo, size);
}

BootfsService::~BootfsService() {
  // Correctly shutting down a memfs (avoiding both use-after-frees and leaks) requires async
  // operations, so we use an Event to wait until the shutdown callback is finished. This is a bit
  // silly and likely won't be exercised outside of tests since bootsvc usually does not terminate
  // normally, but it makes ASAN and LSAN happy.
  zx::event event;
  zx::event::create(0, &event);
  auto callback = [parts(std::move(owned_vmos_)), &event](zx_status_t status) mutable {
    // Bootfs uses multiple Vnodes which may share a reference to a single VMO.
    // Since the lifetime of the VMOs are coupled with the BootfsService, all
    // connections to these Vnodes must be terminated (with Shutdown) before
    // we can safely close the VMOs
    parts.reset();
    event.signal(0, ZX_USER_SIGNAL_0);
  };
  vfs_->Shutdown(std::move(callback));
  event.wait_one(ZX_USER_SIGNAL_0, zx::deadline_after(zx::min(1)), nullptr);
}

zx_status_t BootfsService::DuplicateAsExecutable(const zx::vmo& vmo, zx::vmo* out_vmo) {
  zx::vmo out;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &out);
  if (status != ZX_OK) {
    return status;
  }

  status = out.replace_as_executable(vmex_rsrc_, &out);
  if (status != ZX_OK) {
    return status;
  }

  *out_vmo = std::move(out);
  return ZX_OK;
}

zx_status_t BootfsService::PublishVmo(std::string_view path, zx::vmo vmo, zx_off_t off,
                                      size_t len) {
  zx_status_t status = PublishUnownedVmo(path, vmo, off, len);
  if (status != ZX_OK) {
    return status;
  }
  owned_vmos_.push_back(std::move(vmo));
  return ZX_OK;
}

zx_status_t BootfsService::PublishUnownedVmo(std::string_view path, const zx::vmo& vmo,
                                             zx_off_t off, size_t len) {
  ZX_ASSERT(root_ != nullptr);
  if (path.empty() || path.front() == '/' || path.front() == '\0') {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<memfs::VnodeDir> vnb(root_);
  while (!path.empty()) {
    std::string_view dir = path.substr(0, path.find('/'));
    if (dir.size() == path.size()) {
      dir = {};
    } else {
      path.remove_prefix(dir.size() + 1);
    }

    if (dir.empty()) {
      if (path.empty() || path.front() == '/' || path.front() == '\0') {
        return ZX_ERR_INVALID_ARGS;
      }
      return vfs_->CreateFromVmo(vnb.get(), path, vmo.get(), off, len);
    }

    fbl::RefPtr<fs::Vnode> out;
    zx_status_t status = vnb->Lookup(dir, &out);
    if (status == ZX_ERR_NOT_FOUND) {
      status = vnb->Create(dir, S_IFDIR, &out);
    }
    if (status != ZX_OK) {
      return status;
    }

    vnb = fbl::RefPtr<memfs::VnodeDir>::Downcast(std::move(out));
  }

  // Should not be reached.
  return ZX_ERR_INTERNAL;
}

void BootfsService::PublishStartupVmos(uint8_t type, const char* debug_type_name) {
  constexpr char kVmoSubdir[] = "kernel/";
  constexpr size_t kVmoSubdirLen = sizeof(kVmoSubdir) - 1;

  for (uint16_t i = 0; true; ++i) {
    zx::vmo owned_vmo(zx_take_startup_handle(PA_HND(type, i)));
    if (!owned_vmo.is_valid()) {
      break;
    }
    // We use an unowned VMO here so we can have some finer control over
    // whether the handle is closed.   This is safe since |owned_vmo| will
    // never be closed before |vmo|.
    zx::unowned_vmo vmo(owned_vmo);

    // The first vDSO is the default vDSO.  Since we've taken the startup
    // handle, launchpad won't find it on its own.  So point launchpad at
    // it instead of closing it.
    const bool is_default_vdso = (type == PA_VMO_VDSO && i == 0);
    if (is_default_vdso) {
      launchpad_set_vdso_vmo(owned_vmo.release());
    }

    // The vDSO VMOs have names like "vdso/default", so those
    // become VMO files at "/boot/kernel/vdso/default".
    char name[kVmoSubdirLen + ZX_MAX_NAME_LEN] = {};
    memcpy(name, kVmoSubdir, kVmoSubdirLen);
    size_t size;
    zx_status_t status =
        vmo->get_property(ZX_PROP_NAME, name + kVmoSubdirLen, sizeof(name) - kVmoSubdirLen);
    if (status != ZX_OK) {
      printf("bootsvc: vmo.get_property on %s %u: %s\n", debug_type_name, i,
             zx_status_get_string(status));
      continue;
    }
    if (strlen(name) == kVmoSubdirLen) {
      // Nameless VMOs do not get published.
      continue;
    }
    status = vmo->get_size(&size);
    if (status != ZX_OK) {
      printf("bootsvc: vmo.get_size on %s %u: %s\n", debug_type_name, i,
             zx_status_get_string(status));
      continue;
    }
    if (size == 0) {
      // Empty VMOs do not get published.
      continue;
    }

    // If the VMO has a precise content size set, use that as the file size.
    uint64_t content_size;
    status = vmo->get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size));
    if (status != ZX_OK) {
      printf("bootsvc: vmo.get_property on %s %u: %s\n", debug_type_name, i,
             zx_status_get_string(status));
      continue;
    }
    if (content_size != 0) {
      size = content_size;
    }

    if (!strcmp(name + kVmoSubdirLen, "crashlog")) {
      // The crashlog has a special home.
      strcpy(name, kLastPanicFilePath);
    }

    if (owned_vmo.is_valid()) {
      status = PublishVmo(name, std::move(owned_vmo), 0, size);
    } else {
      status = PublishUnownedVmo(name, *vmo, 0, size);
    }
    if (status != ZX_OK) {
      printf("bootsvc: failed to add %s %u to filesystem as %s: %s\n", debug_type_name, i, name,
             zx_status_get_string(status));
      continue;
    }
  }
}

}  // namespace bootsvc
