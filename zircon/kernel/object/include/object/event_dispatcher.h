// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EVENT_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EVENT_DISPATCHER_H_

#include <sys/types.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <object/dispatcher.h>
#include <object/handle.h>

class EventDispatcher final
    : public SoloDispatcher<EventDispatcher, ZX_DEFAULT_EVENT_RIGHTS, ZX_EVENT_SIGNALED> {
 public:
  static zx_status_t Create(uint32_t options, KernelHandle<EventDispatcher>* handle,
                            zx_rights_t* rights);

  ~EventDispatcher() final;
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_EVENT; }

 private:
  explicit EventDispatcher(uint32_t options);
};

fbl::RefPtr<EventDispatcher> GetMemPressureEvent(uint32_t kind);

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EVENT_DISPATCHER_H_
