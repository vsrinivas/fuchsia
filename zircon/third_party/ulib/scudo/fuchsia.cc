//===-- fuchsia.cc ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

#if SCUDO_FUCHSIA

#include "common.h"
#include "mutex.h"
#include "string_utils.h"

#include <limits.h> // for PAGE_SIZE
#include <stdlib.h> // for abort()
#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>

namespace scudo {

void yieldPlatform() {
  const zx_status_t Status = _zx_nanosleep(0);
  CHECK_EQ(Status, ZX_OK);
}

uptr getPageSize() { return PAGE_SIZE; }

void NORETURN die() { __builtin_trap(); }

// We zero-initialize the Extra parameter of map(), make sure this is consistent
// with ZX_HANDLE_INVALID.
COMPILER_CHECK(ZX_HANDLE_INVALID == 0);

struct MapInfo {
  zx_handle_t Vmar;
  zx_handle_t Vmo;
  uintptr_t VmarBase;
  uint64_t VmoSize;
};
COMPILER_CHECK(sizeof(MapInfo) <= sizeof(OpaquePlatformData));

static void *allocateVmar(uptr Size, MapInfo *Info, bool AllowNoMem) {
  // Only scenario so far.
  DCHECK(Info);
  DCHECK_EQ(Info->Vmar, ZX_HANDLE_INVALID);

  const zx_status_t Status = _zx_vmar_allocate(
      _zx_vmar_root_self(),
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
      Size, &Info->Vmar, &Info->VmarBase);
  if (Status != ZX_OK) {
    if (Status != ZX_ERR_NO_MEMORY || !AllowNoMem)
      dieOnMapUnmapError(Status == ZX_ERR_NO_MEMORY);
    return nullptr;
  }
  return reinterpret_cast<void *>(Info->VmarBase);
}

void *map(void *Addr, uptr Size, const char *Name, uptr Flags,
          OpaquePlatformData *Extra) {
  DCHECK_EQ(Size % PAGE_SIZE, 0);
  const bool AllowNoMem = !!(Flags & MAP_ALLOWNOMEM);
  MapInfo *Info = reinterpret_cast<MapInfo *>(Extra);

  // For MAP_NOACCESS, just allocate a Vmar and return.
  if (Flags & MAP_NOACCESS)
    return allocateVmar(Size, Info, AllowNoMem);

  const zx_handle_t Vmar = Info ? Info->Vmar : _zx_vmar_root_self();
  CHECK_NE(Vmar, ZX_HANDLE_INVALID);

  zx_status_t Status;
  zx_handle_t Vmo;
  uint64_t VmoSize = 0;
  if (Info && Info->Vmo != ZX_HANDLE_INVALID) {
    // If a Vmo was specified, it's a resize operation.
    CHECK(Addr);
    DCHECK(Flags & MAP_RESIZABLE);
    Vmo = Info->Vmo;
    VmoSize = Info->VmoSize;
    Status = _zx_vmo_set_size(Vmo, VmoSize + Size);
    if (Status != ZX_OK) {
      if (Status != ZX_ERR_NO_MEMORY || !AllowNoMem)
        dieOnMapUnmapError(Status == ZX_ERR_NO_MEMORY);
      return nullptr;
    }
  } else {
    // Otherwise, create a Vmo and set its name.
    Status = _zx_vmo_create(Size, ZX_VMO_RESIZABLE, &Vmo);
    if (Status != ZX_OK) {
      if (Status != ZX_ERR_NO_MEMORY || !AllowNoMem)
        dieOnMapUnmapError(Status == ZX_ERR_NO_MEMORY);
      return nullptr;
    }
    _zx_object_set_property(Vmo, ZX_PROP_NAME, Name, strlen(Name));
  }

  uintptr_t P;
  zx_vm_option_t MapFlags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  const uint64_t Offset =
      Addr ? reinterpret_cast<uintptr_t>(Addr) - Info->VmarBase : 0;
  if (Offset)
    MapFlags |= ZX_VM_SPECIFIC;
  Status = _zx_vmar_map(Vmar, MapFlags, Offset, Vmo, VmoSize, Size, &P);
  // No need to track the Vmo if we don't intend on resizing it. Close it.
  if (Flags & MAP_RESIZABLE) {
    DCHECK(Info);
    DCHECK_EQ(Info->Vmo, ZX_HANDLE_INVALID);
    Info->Vmo = Vmo;
  } else {
    CHECK_EQ(_zx_handle_close(Vmo), ZX_OK);
  }
  if (Status != ZX_OK) {
    if (Status != ZX_ERR_NO_MEMORY || !AllowNoMem)
      dieOnMapUnmapError(Status == ZX_ERR_NO_MEMORY);
    return nullptr;
  }
  if (Info)
    Info->VmoSize += Size;

  return reinterpret_cast<void *>(P);
}

void unmap(void *Addr, uptr Size, uptr Flags, OpaquePlatformData *Extra) {
  MapInfo *Info = reinterpret_cast<MapInfo *>(Extra);
  if (Flags & UNMAP_ALL) {
    DCHECK_NE(Info, nullptr);
    const zx_handle_t Vmar = Info->Vmar;
    DCHECK_NE(Vmar, _zx_vmar_root_self());
    // Destroying the vmar effectively unmaps the whole mapping.
    CHECK_EQ(_zx_vmar_destroy(Vmar), ZX_OK);
    CHECK_EQ(_zx_handle_close(Vmar), ZX_OK);
  } else {
    const zx_handle_t Vmar = Info ? Info->Vmar : _zx_vmar_root_self();
    const zx_status_t Status =
        _zx_vmar_unmap(Vmar, reinterpret_cast<uintptr_t>(Addr), Size);
    if (Status != ZX_OK)
      dieOnMapUnmapError();
  }
  if (Info) {
    if (Info->Vmo != ZX_HANDLE_INVALID)
      CHECK_EQ(_zx_handle_close(Info->Vmo), ZX_OK);
    memset(Info, 0, sizeof(*Info));
  }
}

void releasePagesToOS(UNUSED uptr BaseAddress, uptr Offset, uptr Size,
                      OpaquePlatformData *Extra) {
  MapInfo *Info = reinterpret_cast<MapInfo *>(Extra);
  DCHECK(Info);
  DCHECK_NE(Info->Vmar, ZX_HANDLE_INVALID);
  DCHECK_NE(Info->Vmo, ZX_HANDLE_INVALID);
  const zx_status_t Status =
      _zx_vmo_op_range(Info->Vmo, ZX_VMO_OP_DECOMMIT, Offset, Size, NULL, 0);
  CHECK_EQ(Status, ZX_OK);
}

const char *getEnv(const char *Name) { return getenv(Name); }

void BlockingMutex::wait() {
  const zx_status_t Status =
      _zx_futex_wait(reinterpret_cast<zx_futex_t *>(OpaqueStorage), MtxSleeping,
                     ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
  if (Status != ZX_ERR_BAD_STATE)
    CHECK_EQ(Status, ZX_OK); // Normal race
}

void BlockingMutex::wake() {
  const zx_status_t Status =
      _zx_futex_wake(reinterpret_cast<zx_futex_t *>(OpaqueStorage), 1);
  CHECK_EQ(Status, ZX_OK);
}

u64 getMonotonicTime() { return _zx_clock_get_monotonic(); }

u32 getNumberOfCPUs() { return _zx_system_get_num_cpus(); }

bool getRandom(void *Buffer, uptr Length, bool Blocking) {
  COMPILER_CHECK(MaxRandomLength <= ZX_CPRNG_DRAW_MAX_LEN);
  if (!Buffer || !Length || Length > MaxRandomLength)
    return false;
  _zx_cprng_draw(Buffer, Length);
  return true;
}

void outputRaw(const char *Buffer) {
  __sanitizer_log_write(Buffer, strlen(Buffer));
}

void setAbortMessage(const char *Message) {}

} // namespace scudo

#endif // SCUDO_FUCHSIA
