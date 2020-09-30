// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/display_handle.h"

#include <zircon/features.h>
#include <zircon/rights.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/system.h>
#include <zircon/types.h>

#include <iomanip>

namespace fidl_codec {
namespace {

constexpr int kUint32Precision = 8;

}  // namespace

#define ObjTypeNameCase(name) \
  case name:                  \
    printer << #name;         \
    return

void ObjTypeName(zx_obj_type_t obj_type, PrettyPrinter& printer) {
  switch (obj_type) {
    ObjTypeNameCase(ZX_OBJ_TYPE_NONE);
    ObjTypeNameCase(ZX_OBJ_TYPE_PROCESS);
    ObjTypeNameCase(ZX_OBJ_TYPE_THREAD);
    ObjTypeNameCase(ZX_OBJ_TYPE_VMO);
    ObjTypeNameCase(ZX_OBJ_TYPE_CHANNEL);
    ObjTypeNameCase(ZX_OBJ_TYPE_EVENT);
    ObjTypeNameCase(ZX_OBJ_TYPE_PORT);
    ObjTypeNameCase(ZX_OBJ_TYPE_INTERRUPT);
    ObjTypeNameCase(ZX_OBJ_TYPE_PCI_DEVICE);
    ObjTypeNameCase(ZX_OBJ_TYPE_LOG);
    ObjTypeNameCase(ZX_OBJ_TYPE_SOCKET);
    ObjTypeNameCase(ZX_OBJ_TYPE_RESOURCE);
    ObjTypeNameCase(ZX_OBJ_TYPE_EVENTPAIR);
    ObjTypeNameCase(ZX_OBJ_TYPE_JOB);
    ObjTypeNameCase(ZX_OBJ_TYPE_VMAR);
    ObjTypeNameCase(ZX_OBJ_TYPE_FIFO);
    ObjTypeNameCase(ZX_OBJ_TYPE_GUEST);
    ObjTypeNameCase(ZX_OBJ_TYPE_VCPU);
    ObjTypeNameCase(ZX_OBJ_TYPE_TIMER);
    ObjTypeNameCase(ZX_OBJ_TYPE_IOMMU);
    ObjTypeNameCase(ZX_OBJ_TYPE_BTI);
    ObjTypeNameCase(ZX_OBJ_TYPE_PROFILE);
    ObjTypeNameCase(ZX_OBJ_TYPE_PMT);
    ObjTypeNameCase(ZX_OBJ_TYPE_SUSPEND_TOKEN);
    ObjTypeNameCase(ZX_OBJ_TYPE_PAGER);
    ObjTypeNameCase(ZX_OBJ_TYPE_EXCEPTION);
    ObjTypeNameCase(ZX_OBJ_TYPE_CLOCK);
    ObjTypeNameCase(ZX_OBJ_TYPE_STREAM);
    ObjTypeNameCase(ZX_OBJ_TYPE_MSI_ALLOCATION);
    ObjTypeNameCase(ZX_OBJ_TYPE_MSI_INTERRUPT);
    default:
      printer << obj_type;
      return;
  }
}

#define ShortObjTypeNameCase(name, text) \
  case name:                             \
    printer << text;                     \
    return

void ShortObjTypeName(zx_obj_type_t obj_type, PrettyPrinter& printer) {
  switch (obj_type) {
    ShortObjTypeNameCase(ZX_OBJ_TYPE_NONE, "None");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_PROCESS, "Process");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_THREAD, "Thread");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_VMO, "Vmo");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_CHANNEL, "Channel");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_EVENT, "Event");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_PORT, "Port");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_INTERRUPT, "Interrupt");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_PCI_DEVICE, "PciDevice");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_LOG, "Log");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_SOCKET, "Socket");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_RESOURCE, "Resource");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_EVENTPAIR, "EventPair");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_JOB, "Job");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_VMAR, "Vmar");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_FIFO, "Fifo");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_GUEST, "Guest");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_VCPU, "Vcpu");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_TIMER, "Timer");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_IOMMU, "IoMmu");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_BTI, "Bti");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_PROFILE, "Profile");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_PMT, "Pmt");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_SUSPEND_TOKEN, "SuspendToken");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_PAGER, "Pager");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_EXCEPTION, "Exception");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_CLOCK, "Clock");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_STREAM, "Stream");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_MSI_ALLOCATION, "MsiAllocation");
    ShortObjTypeNameCase(ZX_OBJ_TYPE_MSI_INTERRUPT, "MsiInterrupt");
    default:
      printer << obj_type;
      return;
  }
}

void DisplayHandle(const zx_handle_info_t& handle, PrettyPrinter& printer) {
  printer << Red;
  if (handle.type != ZX_OBJ_TYPE_NONE) {
    ShortObjTypeName(handle.type, printer);
    printer << ':';
  }
  char buffer[kUint32Precision + 1];
  snprintf(buffer, sizeof(buffer), "%08x", handle.handle);
  printer << buffer;
  printer << ResetColor;
  if (handle.rights != 0) {
    printer << '(';
    printer.DisplayRights(handle.rights);
    printer << ')';
  }
}

}  // namespace fidl_codec
