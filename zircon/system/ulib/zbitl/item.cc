// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/item.h>

#include <string_view>

using namespace std::literals;

namespace zbitl {

std::string_view TypeName(uint32_t type) {
#define TYPE(code, name, extension) \
  case code:                        \
    return name##sv;
  switch (type) { ZBI_ALL_TYPES(TYPE) }
#undef TYPE
  return {};
}

std::string_view TypeExtension(uint32_t type) {
#define TYPE(code, name, extension) \
  case code:                        \
    return extension##sv;
  switch (type) { ZBI_ALL_TYPES(TYPE) }
#undef TYPE
  return {};
}

bool TypeIsStorage(uint32_t type) {
  // TODO(mcgrathr): Ideally we'd encode this as a mask of type bits or
  // something else simple.  Short of that, someplace more authoritative than
  // here should contain this list.  But N.B. that no ZBI_TYPE_STORAGE_* type
  // is a long-term stable protocol with boot loaders, so meh.
  //
  // TODO(mcgrathr): ZBI_TYPE_STORAGE_BOOTFS_FACTORY is misnamed and is not
  // actually a "storage" type.
  switch (type) {
    case ZBI_TYPE_STORAGE_RAMDISK:
    case ZBI_TYPE_STORAGE_BOOTFS:
      return true;
  }
  return false;
}

}  // namespace zbitl
