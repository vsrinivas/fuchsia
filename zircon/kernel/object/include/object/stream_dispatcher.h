// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_STREAM_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_STREAM_DISPATCHER_H_

#include <lib/user_copy/user_iovec.h>
#include <zircon/types.h>

#include <fbl/ref_counted.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <object/vm_object_dispatcher.h>
#include <vm/vm_aspace.h>

class StreamDispatcher final : public SoloDispatcher<StreamDispatcher, ZX_DEFAULT_STREAM_RIGHTS> {
 public:
  static zx_status_t Create(uint32_t options, fbl::RefPtr<VmObjectDispatcher> vmo, zx_off_t seek,
                            KernelHandle<StreamDispatcher>* handle, zx_rights_t* rights);
  ~StreamDispatcher();

  zx_obj_type_t get_type() const { return ZX_OBJ_TYPE_STREAM; }

  zx_status_t ReadVector(VmAspace* current_aspace, user_out_iovec_t user_data, size_t* out_actual);
  zx_status_t ReadVectorAt(VmAspace* current_aspace, user_out_iovec_t user_data, zx_off_t offset,
                           size_t* out_actual);
  zx_status_t WriteVector(VmAspace* current_aspace, user_in_iovec_t user_data, size_t* out_actual);
  zx_status_t WriteVectorAt(VmAspace* current_aspace, user_in_iovec_t user_data, zx_off_t offset,
                            size_t* out_actual);
  zx_status_t AppendVector(VmAspace* current_aspace, user_in_iovec_t user_data, size_t* out_actual);
  zx_status_t Seek(zx_stream_seek_origin_t whence, int64_t offset, zx_off_t* out_seek);

  void GetInfo(zx_info_stream_t* info) const;

 private:
  explicit StreamDispatcher(uint32_t options, fbl::RefPtr<VmObjectDispatcher> vmo, zx_off_t seek);

  // The seek_lock_ is used to synchronize vmo_ operations and updates to seek.
  // Ideally the existing dispatchers lock would be used, but presently it is possible for page
  // requests to get waited on while this lock is held due to calls to vmo_->ExpandContentIfNeeded
  // being able to block, and so prefer to use a separate lock that we can add instrumentation to
  // without needing to change the entire dispatcher lock.
  // TODO: Remove this and use dispatcher lock once content size operations will not block.
  mutable DECLARE_MUTEX(StreamDispatcher) seek_lock_;

  const uint32_t options_;
  const fbl::RefPtr<VmObjectDispatcher> vmo_;
  zx_off_t seek_ TA_GUARDED(seek_lock_) = 0u;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_STREAM_DISPATCHER_H_
