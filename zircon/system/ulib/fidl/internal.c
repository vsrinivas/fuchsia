// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

// Coding tables for primitives are predefined and interned here.
// This file must be a .c to guarantee that these types are stored directly in
// .rodata, rather than requiring global ctors to have been run (fxbug.dev/39978).
const struct FidlCodedPrimitive fidl_internal_kBoolTable = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Bool};
const struct FidlCodedPrimitive fidl_internal_kInt8Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Int8};
const struct FidlCodedPrimitive fidl_internal_kInt16Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Int16};
const struct FidlCodedPrimitive fidl_internal_kInt32Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Int32};
const struct FidlCodedPrimitive fidl_internal_kInt64Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Int64};
const struct FidlCodedPrimitive fidl_internal_kUint8Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Uint8};
const struct FidlCodedPrimitive fidl_internal_kUint16Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Uint16};
const struct FidlCodedPrimitive fidl_internal_kUint32Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Uint32};
const struct FidlCodedPrimitive fidl_internal_kUint64Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Uint64};
const struct FidlCodedPrimitive fidl_internal_kFloat32Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Float32};
const struct FidlCodedPrimitive fidl_internal_kFloat64Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Float64};

zx_rights_t subtract_rights(zx_rights_t minuend, zx_rights_t subtrahend) {
  return minuend & ~subtrahend;
}

zx_status_t FidlEnsureHandleRights(zx_handle_t* handle_ptr, zx_obj_type_t actual_type,
                                   zx_obj_type_t actual_rights, zx_obj_type_t required_object_type,
                                   zx_rights_t required_rights, const char** error) {
  // Note: objects returned from the kernel should never have type
  // ZX_OBJ_TYPE_NONE, however this is used for backwards compatibility in
  // some places.
  if (unlikely(required_object_type != actual_type && required_object_type != ZX_OBJ_TYPE_NONE &&
               actual_type != ZX_OBJ_TYPE_NONE)) {
#ifdef __Fuchsia__
    zx_handle_close(*handle_ptr);
#endif
    *handle_ptr = ZX_HANDLE_INVALID;
    if (error) {
      *error = "object type does not match expected type";
    }
    return ZX_ERR_INVALID_ARGS;
  }

  // Special case: ZX_HANDLE_SAME_RIGHTS allows all handles through unchanged.
  // Note: objects returned from the kernel should never have rights
  // ZX_RIGHT_SAME_RIGHTS, however this is used for backwards compatibility
  // in some places.
  if (required_rights == ZX_RIGHT_SAME_RIGHTS || actual_rights == ZX_RIGHT_SAME_RIGHTS) {
    return ZX_OK;
  }

  // Check that |actual_rights| contain all of the |required_rights|.
  if (unlikely(subtract_rights(required_rights, actual_rights) != 0)) {
#ifdef __Fuchsia__
    zx_handle_close(*handle_ptr);
#endif
    *handle_ptr = ZX_HANDLE_INVALID;
    if (error) {
      *error = "missing required rights";
    }
    return ZX_ERR_INVALID_ARGS;
  }

  // Check if |actual_rights| has more rights than |required_rights|.
  // If so, the rights need to be reduced.
  if (unlikely(subtract_rights(actual_rights, required_rights))) {
#ifdef __Fuchsia__
    zx_status_t status = zx_handle_replace(*handle_ptr, required_rights, handle_ptr);
    if (unlikely(status != ZX_OK)) {
      if (error)
        *error = "zx_handle_replace failed";
      return status;
    }
#else
    ZX_PANIC("zx_handle_replace only supported on Fuchsia");
#endif
  }
  return ZX_OK;
}

zx_status_t FidlHandleDispositionsToHandleInfos(zx_handle_disposition_t* handle_dispositions,
                                                zx_handle_info_t* handle_infos,
                                                uint32_t num_handles) {
  for (size_t i = 0; i < num_handles; i++) {
    zx_handle_disposition_t* handle_disposition = &handle_dispositions[i];
    if (handle_disposition->operation != ZX_HANDLE_OP_MOVE) {
      FidlHandleDispositionCloseMany(handle_dispositions, num_handles);
      FidlHandleInfoCloseMany(handle_infos, i);
      return ZX_ERR_INVALID_ARGS;
    }
    if (handle_disposition->result != ZX_OK) {
      FidlHandleDispositionCloseMany(handle_dispositions, num_handles);
      FidlHandleInfoCloseMany(handle_infos, i);
      return ZX_ERR_INVALID_ARGS;
    }
#ifdef __Fuchsia__
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle_disposition->handle, ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), NULL, NULL);
    if (status != ZX_OK) {
      FidlHandleDispositionCloseMany(handle_dispositions, num_handles);
      FidlHandleInfoCloseMany(handle_infos, i);
      return status;
    }

    zx_handle_t handle = handle_disposition->handle;
    handle_disposition->handle = ZX_HANDLE_INVALID;
    status = FidlEnsureHandleRights(&handle, info.type, info.rights, handle_disposition->type,
                                    handle_disposition->rights, NULL);
    if (status != ZX_OK) {
      FidlHandleDispositionCloseMany(handle_dispositions, num_handles);
      FidlHandleInfoCloseMany(handle_infos, i);
      return status;
    }
    handle_infos[i].handle = handle;
    handle_infos[i].type = info.type;
    handle_infos[i].rights = info.rights;
#else
    ZX_PANIC("zx_object_get_info unsupported on host");
#endif
  }
  return ZX_OK;
}
