#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include "stdio_impl.h"
#include "threads_impl.h"
#include "zircon_impl.h"

static const char kMmapAnonymousVmoName[] = "mmap-anonymous";

static inline void* mmap_error(zx_status_t status);

// mmap implementation
void* __mmap(void* start, size_t len, int prot, int flags, int fd, off_t fd_off) {
  if (fd_off & (PAGE_SIZE - 1)) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  if (len == 0) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  if (len >= PTRDIFF_MAX) {
    errno = ENOMEM;
    return MAP_FAILED;
  }
  if (!(flags & (MAP_PRIVATE | MAP_SHARED)) || (flags & MAP_PRIVATE && flags & MAP_SHARED)) {
    errno = EINVAL;
    return MAP_FAILED;
  }

  // The POSIX standard requires the file be opened with read permission regardless of the specified
  // PROT_* flags. Implementations are permitted to provide access types exceeding those requested
  // (e.g. PROT_WRITE may imply PROT_READ as well). Since zx_vmar_map currently disallows mapping
  // writable/executable VMOs without read rights, we must set PROT_READ if either PROT_WRITE or
  // PROT_EXEC is specified.
  // https://cs.opensource.google/fuchsia/fuchsia/+/5cbdfbb2a7be562a76095a384efca39dac00d477:zircon/kernel/object/vm_address_region_dispatcher.cc;l=260
  prot |= (prot & (PROT_WRITE | PROT_EXEC)) ? PROT_READ : 0;

  // round up to page size
  len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  zx_vm_option_t zx_options = 0;
  zx_options |= (prot & PROT_READ) ? ZX_VM_PERM_READ : 0;
  zx_options |= (prot & PROT_WRITE) ? ZX_VM_PERM_WRITE : 0;
  zx_options |= (prot & PROT_EXEC) ? ZX_VM_PERM_EXECUTE : 0;

  size_t offset = 0;
  if (flags & MAP_FIXED) {
    zx_options |= ZX_VM_SPECIFIC_OVERWRITE;
    zx_info_vmar_t info;
    zx_status_t status =
        _zx_object_get_info(_zx_vmar_root_self(), ZX_INFO_VMAR, &info, sizeof(info), NULL, NULL);
    if (status != ZX_OK || (uintptr_t)start < info.base) {
      return mmap_error(status);
    }
    offset = (uintptr_t)start - info.base;
  }

  // Either create a new VMO if this is an anonymous mapping, or obtain one from the backing fd.
  zx_handle_t vmo;
  if (flags & MAP_ANON) {
    zx_status_t status = _zx_vmo_create(len, 0, &vmo);
    if (status != ZX_OK) {
      return mmap_error(status);
    }

    status = _zx_object_set_property(vmo, ZX_PROP_NAME, kMmapAnonymousVmoName,
                                     (sizeof kMmapAnonymousVmoName) - 1);
    if (status != ZX_OK) {
      zx_status_t close_status = _zx_handle_close(vmo);
      ZX_ASSERT_MSG(close_status == ZX_OK, "Failed to close VMO: %s",
                    _zx_status_get_string(close_status));
      return mmap_error(status);
    }

    if (flags & MAP_JIT) {
      status = _zx_vmo_replace_as_executable(vmo, ZX_HANDLE_INVALID, &vmo);
      if (status != ZX_OK) {
        return mmap_error(status);
      }
    }
  } else {
    zx_options |= ZX_VM_ALLOW_FAULTS;
    zx_status_t status = _mmap_get_vmo_from_fd(prot, flags, fd, &vmo);
    if (status != ZX_OK) {
      return mmap_error(status);
    }
  }

  // Map the VMO with the specified options.
  uintptr_t ptr = 0;
  zx_status_t status =
      _zx_vmar_map(_zx_vmar_root_self(), zx_options, offset, vmo, fd_off, len, &ptr);
  // The VMAR keeps an internal handle to the mapped VMO, so we can close the existing handle.
  zx_status_t close_status = _zx_handle_close(vmo);
  ZX_ASSERT_MSG(close_status == ZX_OK, "Failed to close VMO: %s",
                _zx_status_get_string(close_status));
  // TODO: map this as shared if we ever implement forking
  if (status != ZX_OK) {
    return mmap_error(status);
  }

  return (void*)ptr;
}

// Set errno based on the given status and return MAP_FAILED.
static inline void* mmap_error(zx_status_t status) {
  switch (status) {
    case ZX_ERR_BAD_HANDLE:
      errno = EBADF;
      break;
    case ZX_ERR_NOT_SUPPORTED:
      errno = ENODEV;
      break;
    case ZX_ERR_ACCESS_DENIED:
      errno = EACCES;
      break;
    case ZX_ERR_NO_MEMORY:
      errno = ENOMEM;
      break;
    case ZX_ERR_INVALID_ARGS:
    case ZX_ERR_BAD_STATE:
    default:
      errno = EINVAL;
  }
  return MAP_FAILED;
}

weak_alias(__mmap, mmap);
