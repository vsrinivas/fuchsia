// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/userabi/userboot_internal.h>
#include <zircon/assert.h>

#include <ktl/move.h>
#include <object/handle.h>
#include <object/resource_dispatcher.h>

#include <ktl/enforce.h>

HandleOwner get_resource_handle(zx_rsrc_kind_t kind) {
  char name[ZX_MAX_NAME_LEN];
  switch (kind) {
    case ZX_RSRC_KIND_MMIO:
      strlcpy(name, "mmio", ZX_MAX_NAME_LEN);
      break;
    case ZX_RSRC_KIND_IRQ:
      strlcpy(name, "irq", ZX_MAX_NAME_LEN);
      break;
    case ZX_RSRC_KIND_IOPORT:
      strlcpy(name, "io_port", ZX_MAX_NAME_LEN);
      break;
    case ZX_RSRC_KIND_ROOT:
      strlcpy(name, "root", ZX_MAX_NAME_LEN);
      break;
    case ZX_RSRC_KIND_SMC:
      strlcpy(name, "smc", ZX_MAX_NAME_LEN);
      break;
    case ZX_RSRC_KIND_SYSTEM:
      strlcpy(name, "system", ZX_MAX_NAME_LEN);
      break;
  }
  zx_rights_t rights;
  KernelHandle<ResourceDispatcher> rsrc;
  zx_status_t result;
  switch (kind) {
    case ZX_RSRC_KIND_ROOT:
      result = ResourceDispatcher::Create(&rsrc, &rights, kind, 0, 0, 0, name);
      break;
    case ZX_RSRC_KIND_MMIO:
    case ZX_RSRC_KIND_IRQ:
#if ARCH_X86
    case ZX_RSRC_KIND_IOPORT:
#elif ARCH_ARM64
    case ZX_RSRC_KIND_SMC:
#endif
    case ZX_RSRC_KIND_SYSTEM:
      result = ResourceDispatcher::CreateRangedRoot(&rsrc, &rights, kind, name);
      break;
    default:
      result = ZX_ERR_WRONG_TYPE;
      break;
  }
  ZX_ASSERT(result == ZX_OK);
  return Handle::Make(ktl::move(rsrc), rights);
}
