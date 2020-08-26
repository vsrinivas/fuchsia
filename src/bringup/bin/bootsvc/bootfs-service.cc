// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootfs-service.h"

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
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
#include <fs/vfs_types.h>
#include <launchpad/launchpad.h>

#include "src/lib/bootfs/parser.h"
#include "util.h"

namespace fio = ::llcpp::fuchsia::io;

namespace bootsvc {

namespace {

// 'Packages' in bootfs can contain executable files but we need to account for the package name
// path component, which can be anything. For example, 'pkg/my_package/bin' should be executable but
// 'pkg/my_package/foo' should not.
static constexpr const char* kBootfsPackagePrefix = "pkg/";
static constexpr const char* kExecutablePackageDirectories[] = {
    "bin/",
    "lib/",
};

static bool PathInExecutablePackageDirectory(const char* path) {
  // All packages in bootfs are located under a single directory.
  if (strncmp(kBootfsPackagePrefix, path, strlen(kBootfsPackagePrefix)) != 0) {
    return false;
  }

  // Advance past the path separator separating the package name and path inside the package.
  const char* inside_pkg = strchr(path + strlen(kBootfsPackagePrefix), '/');
  if (inside_pkg == nullptr) {
    return false;
  }
  inside_pkg++;

  // Finally, check if the path inside the package is one of the allowlisted paths.
  for (const char* prefix : kExecutablePackageDirectories) {
    if (strncmp(prefix, inside_pkg, strlen(prefix)) == 0) {
      return true;
    }
  }
  return false;
}

// Other top-level directories in bootfs that are allowed to contain executable files (i.e. files
// for which bootfs should allow opening with OPEN_RIGHT_EXECUTABLE).
static constexpr const char* kExecutableDirectories[] = {
    "bin/",
    "driver/",
    "lib/",
    "test/",
};

static bool PathInExecutableDirectory(const char* path) {
  if (PathInExecutablePackageDirectory(path)) {
    return true;
  }

  for (const char* prefix : kExecutableDirectories) {
    if (strncmp(prefix, path, strlen(prefix)) == 0) {
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

  zx_status_t status = memfs::Vfs::Create("<root>", &svc->vfs_, &svc->root_);
  if (status != ZX_OK) {
    return status;
  }

  svc->vfs_->SetDispatcher(dispatcher);
  *out = std::move(svc);
  return ZX_OK;
}

zx_status_t BootfsService::AddBootfs(zx::vmo bootfs_vmo) {
  bootfs::Parser parser;
  zx_status_t status = parser.Init(zx::unowned_vmo(bootfs_vmo));
  if (status != ZX_OK) {
    return status;
  }

  // The bootfs VnodeVmo nodes are all created from the same backing VMO with differing offsets, and
  // memfs creates clones as needed. Executable files use a duplicate handle to this same VMO which
  // has ZX_RIGHT_EXECUTE added. This is done once here rather than in PublishUnownedVmo to avoid
  // lots of repetitive unnecessary syscalls for every executable file.
  zx::vmo bootfs_exec_vmo;
  status = DuplicateAsExecutable(bootfs_vmo, &bootfs_exec_vmo);
  if (status != ZX_OK) {
    return status;
  }

  // Load all of the entries in the bootfs into the FS
  status = parser.Parse([this, &bootfs_vmo,
                         &bootfs_exec_vmo](const zbi_bootfs_dirent_t* entry) -> zx_status_t {
    const zx::vmo& vmo = (PathInExecutableDirectory(entry->name)) ? bootfs_exec_vmo : bootfs_vmo;
    PublishUnownedVmo(entry->name, vmo, entry->data_off, entry->data_len);
    return ZX_OK;
  });
  // Add these VMOs to our list of owned VMOs even on failure, since we may have
  // added a file
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
  uint32_t vmo_flags = fio::VMO_FLAG_READ | fio::VMO_FLAG_PRIVATE;
  vmo_flags |= executable ? fio::VMO_FLAG_EXEC : 0;
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

zx_status_t BootfsService::PublishVmo(const char* path, zx::vmo vmo, zx_off_t off, size_t len) {
  zx_status_t status = PublishUnownedVmo(path, vmo, off, len);
  if (status != ZX_OK) {
    return status;
  }
  owned_vmos_.push_back(std::move(vmo));
  return ZX_OK;
}

zx_status_t BootfsService::PublishUnownedVmo(const char* path, const zx::vmo& vmo, zx_off_t off,
                                             size_t len) {
  ZX_ASSERT(root_ != nullptr);
  if ((path[0] == '/') || (path[0] == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<memfs::VnodeDir> vnb(root_);
  while (true) {
    const char* nextpath = strchr(path, '/');
    if (nextpath == nullptr) {
      if (path[0] == 0) {
        return ZX_ERR_INVALID_ARGS;
      }
      return vfs_->CreateFromVmo(vnb.get(), std::string_view(path, strlen(path)), vmo.get(), off,
                                 len);
    } else {
      if (nextpath == path) {
        return ZX_ERR_INVALID_ARGS;
      }

      fbl::RefPtr<fs::Vnode> out;
      zx_status_t status = vnb->Lookup(&out, std::string_view(path, nextpath - path));
      if (status == ZX_ERR_NOT_FOUND) {
        status = vnb->Create(&out, std::string_view(path, nextpath - path), S_IFDIR);
      }
      if (status != ZX_OK) {
        return status;
      }
      vnb = fbl::RefPtr<memfs::VnodeDir>::Downcast(std::move(out));
      path = nextpath + 1;
    }
  }
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
