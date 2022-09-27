// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TYPES_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TYPES_H_

#include <stdint.h>

namespace fidl::types {

// TODO(fxbug.dev/51002): zircon/types.h's zx_rights_t needs to be kept in sync with
// this. Eventually, zx_rights_t will be generated from a fidl
// declaration. This is currently tested by fidl-compiler's
// TypesTests' rights test.
using RightsWrappedType = uint32_t;

enum struct Nullability {
  kNullable,
  kNonnullable,
};

enum struct Strictness {
  kFlexible,
  kStrict,
};

enum struct Resourceness {
  kValue,
  kResource,
};

enum struct Openness {
  kClosed,
  kAjar,
  kOpen,
};

// TODO(fxbug.dev/51002): zircon/types.h's zx_obj_type_t and related values must be
// kept in sync with this. Eventually, they will be generated from
// fidl declarations. This is currently tested by fidl-compiler's
// TypesTests's handle_subtype test.
// TODO(fxbug.dev/64629): Remove this enumeration once handle generalization is
// fully implemented. The enum `obj_type` defined in the FIDL library zx will
// become the only source of truth.
enum struct HandleSubtype : uint32_t {
  // special case to indicate subtype is not specified.
  kHandle = 0,

  kBti = 24,
  kChannel = 4,
  kClock = 30,
  kEvent = 5,
  kEventpair = 16,
  kException = 29,
  kFifo = 19,
  kGuest = 20,
  kInterrupt = 9,
  kIommu = 23,
  kJob = 17,
  kLog = 12,
  kMsi = 32,
  kPager = 28,
  kPciDevice = 11,
  kPmt = 26,
  kPort = 6,
  kProcess = 1,
  kProfile = 25,
  kResource = 15,
  kSocket = 14,
  kStream = 31,
  kSuspendToken = 27,
  kThread = 2,
  kTimer = 22,
  kVcpu = 21,
  kVmar = 18,
  kVmo = 3,
};

enum struct PrimitiveSubtype {
  kBool,
  kInt8,
  kInt16,
  kInt32,
  kInt64,
  kUint8,
  kUint16,
  kUint32,
  kUint64,
  kZxUsize,
  kFloat32,
  kFloat64,
};

enum struct InternalSubtype {
  kTransportErr,
};

enum struct MessageKind {
  kRequest,
  kResponse,
  kEvent,
};

}  // namespace fidl::types

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TYPES_H_
