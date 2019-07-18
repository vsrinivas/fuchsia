// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/type_decoder.h"

#include <zircon/system/public/zircon/rights.h>
#include <zircon/system/public/zircon/syscalls/object.h>
#include <zircon/system/public/zircon/types.h>

#include <cstdint>
#include <iomanip>
#include <ostream>

namespace fidlcat {

#define ErrorNameCase(name) \
  case name:                \
    os << #name;            \
    return

// TODO: (use zx_status_get_string when it will be available).
void ErrorName(int64_t error_code, std::ostream& os) {
  switch (error_code) {
    ErrorNameCase(ZX_ERR_INTERNAL);
    ErrorNameCase(ZX_ERR_NOT_SUPPORTED);
    ErrorNameCase(ZX_ERR_NO_RESOURCES);
    ErrorNameCase(ZX_ERR_NO_MEMORY);
    ErrorNameCase(ZX_ERR_INTERNAL_INTR_RETRY);
    ErrorNameCase(ZX_ERR_INVALID_ARGS);
    ErrorNameCase(ZX_ERR_BAD_HANDLE);
    ErrorNameCase(ZX_ERR_WRONG_TYPE);
    ErrorNameCase(ZX_ERR_BAD_SYSCALL);
    ErrorNameCase(ZX_ERR_OUT_OF_RANGE);
    ErrorNameCase(ZX_ERR_BUFFER_TOO_SMALL);
    ErrorNameCase(ZX_ERR_BAD_STATE);
    ErrorNameCase(ZX_ERR_TIMED_OUT);
    ErrorNameCase(ZX_ERR_SHOULD_WAIT);
    ErrorNameCase(ZX_ERR_CANCELED);
    ErrorNameCase(ZX_ERR_PEER_CLOSED);
    ErrorNameCase(ZX_ERR_NOT_FOUND);
    ErrorNameCase(ZX_ERR_ALREADY_EXISTS);
    ErrorNameCase(ZX_ERR_ALREADY_BOUND);
    ErrorNameCase(ZX_ERR_UNAVAILABLE);
    ErrorNameCase(ZX_ERR_ACCESS_DENIED);
    ErrorNameCase(ZX_ERR_IO);
    ErrorNameCase(ZX_ERR_IO_REFUSED);
    ErrorNameCase(ZX_ERR_IO_DATA_INTEGRITY);
    ErrorNameCase(ZX_ERR_IO_DATA_LOSS);
    ErrorNameCase(ZX_ERR_IO_NOT_PRESENT);
    ErrorNameCase(ZX_ERR_IO_OVERRUN);
    ErrorNameCase(ZX_ERR_IO_MISSED_DEADLINE);
    ErrorNameCase(ZX_ERR_IO_INVALID);
    ErrorNameCase(ZX_ERR_BAD_PATH);
    ErrorNameCase(ZX_ERR_NOT_DIR);
    ErrorNameCase(ZX_ERR_NOT_FILE);
    ErrorNameCase(ZX_ERR_FILE_BIG);
    ErrorNameCase(ZX_ERR_NO_SPACE);
    ErrorNameCase(ZX_ERR_NOT_EMPTY);
    ErrorNameCase(ZX_ERR_STOP);
    ErrorNameCase(ZX_ERR_NEXT);
    ErrorNameCase(ZX_ERR_ASYNC);
    ErrorNameCase(ZX_ERR_PROTOCOL_NOT_SUPPORTED);
    ErrorNameCase(ZX_ERR_ADDRESS_UNREACHABLE);
    ErrorNameCase(ZX_ERR_ADDRESS_IN_USE);
    ErrorNameCase(ZX_ERR_NOT_CONNECTED);
    ErrorNameCase(ZX_ERR_CONNECTION_REFUSED);
    ErrorNameCase(ZX_ERR_CONNECTION_RESET);
    ErrorNameCase(ZX_ERR_CONNECTION_ABORTED);
    default:
      os << "errno=" << error_code;
      return;
  }
}

#define ObjTypeNameCase(name) \
  case name:                  \
    os << #name;              \
    return

void ObjTypeName(zx_obj_type_t obj_type, std::ostream& os) {
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
    default:
      os << obj_type;
      return;
  }
}

#define RightsNameCase(name)  \
  if ((rights & name) != 0) { \
    os << separator << #name; \
    separator = " | ";        \
  }

void RightsName(zx_rights_t rights, std::ostream& os) {
  if (rights == 0) {
    os << "ZX_RIGHT_NONE";
    return;
  }
  const char* separator = "";
  RightsNameCase(ZX_RIGHT_DUPLICATE);
  RightsNameCase(ZX_RIGHT_TRANSFER);
  RightsNameCase(ZX_RIGHT_READ);
  RightsNameCase(ZX_RIGHT_WRITE);
  RightsNameCase(ZX_RIGHT_EXECUTE);
  RightsNameCase(ZX_RIGHT_MAP);
  RightsNameCase(ZX_RIGHT_GET_PROPERTY);
  RightsNameCase(ZX_RIGHT_SET_PROPERTY);
  RightsNameCase(ZX_RIGHT_ENUMERATE);
  RightsNameCase(ZX_RIGHT_DESTROY);
  RightsNameCase(ZX_RIGHT_SET_POLICY);
  RightsNameCase(ZX_RIGHT_GET_POLICY);
  RightsNameCase(ZX_RIGHT_SIGNAL);
  RightsNameCase(ZX_RIGHT_SIGNAL_PEER);
  RightsNameCase(ZX_RIGHT_WAIT);
  RightsNameCase(ZX_RIGHT_INSPECT);
  RightsNameCase(ZX_RIGHT_MANAGE_JOB);
  RightsNameCase(ZX_RIGHT_MANAGE_PROCESS);
  RightsNameCase(ZX_RIGHT_MANAGE_THREAD);
  RightsNameCase(ZX_RIGHT_APPLY_PROFILE);
  RightsNameCase(ZX_RIGHT_SAME_RIGHTS);
}

void DisplayHandle(const Colors& colors, const zx_handle_info_t& handle, std::ostream& os) {
  os << colors.red;
  if (handle.type != ZX_OBJ_TYPE_NONE) {
    ObjTypeName(handle.type, os);
    os << ':';
  }
  os << std::hex << std::setfill('0') << std::setw(8) << handle.handle << std::dec << std::setw(0);
  if (handle.rights != 0) {
    os << colors.blue << '(';
    RightsName(handle.rights, os);
    os << ')';
  }
  os << colors.reset;
}

}  // namespace fidlcat
