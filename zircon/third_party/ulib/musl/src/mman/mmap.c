#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include "stdio_impl.h"
#include "threads_impl.h"
#include "zircon_impl.h"

static zx_status_t mmap_inner(uintptr_t start, size_t len, int prot, int flags, int fd,
                              off_t fd_off, void** context, uintptr_t* ptr) {
  zx_vm_option_t zx_options = 0;
  zx_options |= (prot & PROT_READ) ? ZX_VM_PERM_READ : 0;
  zx_options |= (prot & PROT_WRITE) ? ZX_VM_PERM_WRITE : 0;
  zx_options |= (prot & PROT_EXEC) ? ZX_VM_PERM_EXECUTE : 0;

  size_t offset;
  if (flags & MAP_FIXED) {
    zx_options |= ZX_VM_SPECIFIC_OVERWRITE;
    zx_info_vmar_t info;
    zx_status_t status =
        _zx_object_get_info(_zx_vmar_root_self(), ZX_INFO_VMAR, &info, sizeof(info), NULL, NULL);
    if (status != ZX_OK) {
      return status;
    }
    if (start < info.base) {
      return ZX_ERR_INVALID_ARGS;
    }
    offset = start - info.base;
  } else {
    offset = 0;
  }

  // Either create a new VMO if this is an anonymous mapping, or obtain one from the backing fd.
  zx_handle_t vmo;
  if (flags & MAP_ANON) {
    zx_status_t status = _zx_vmo_create(len, 0, &vmo);
    if (status != ZX_OK) {
      return status;
    }

    static const char kMmapAnonymousVmoName[] = "mmap-anonymous";
    {
      zx_status_t status = _zx_object_set_property(vmo, ZX_PROP_NAME, kMmapAnonymousVmoName,
                                                   (sizeof(kMmapAnonymousVmoName)) - 1);
      ZX_ASSERT_MSG(status == ZX_OK, "failed to set_property(ZX_PROP_NAME): %s",
                    _zx_status_get_string(status));
    }
    if (flags & MAP_JIT) {
      zx_status_t status = _zx_vmo_replace_as_executable(vmo, ZX_HANDLE_INVALID, &vmo);
      if (status != ZX_OK) {
        return status;
      }
    }
  } else {
    *context = _fd_get_context(fd);
    if (context == NULL) {
      return ZX_ERR_BAD_HANDLE;
    }
    zx_options |= ZX_VM_ALLOW_FAULTS;
    zx_status_t status = _mmap_get_vmo_from_context(prot, flags, *context, &vmo);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Map the VMO with the specified options.
  zx_status_t status =
      _zx_vmar_map(_zx_vmar_root_self(), zx_options, offset, vmo, fd_off, len, ptr);
  // The VMAR keeps an internal handle to the mapped VMO, so we can close the existing handle.
  {
    zx_status_t status = _zx_handle_close(vmo);
    ZX_ASSERT_MSG(status == ZX_OK, "failed to handle_close(): %s", _zx_status_get_string(status));
  }
  return status;
}

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

  void* context = NULL;
  uintptr_t ptr_val;
  zx_status_t status =
      mmap_inner((uintptr_t)start, len, prot, flags, fd, fd_off, &context, &ptr_val);
  void* ptr = (void*)ptr_val;
  if (context != NULL) {
    if (status == ZX_OK) {
      status = _mmap_on_mapped(context, ptr);
      if (status != ZX_OK) {
        zx_status_t unmap_status = _zx_vmar_unmap(_zx_vmar_root_self(), ptr_val, len);
        ZX_ASSERT_MSG(unmap_status == ZX_OK, "failed to vmar_unmap(): %s", _zx_status_get_string(unmap_status));
      }
    }
    _fd_release_context(context);
  }
  switch (status) {
    case ZX_OK:
      return ptr;
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
      break;
  }
  return MAP_FAILED;
}

weak_alias(__mmap, mmap);
