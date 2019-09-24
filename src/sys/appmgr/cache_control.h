// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_CACHE_CONTROL_H_
#define SRC_SYS_APPMGR_CACHE_CONTROL_H_

#include <fuchsia/sys/test/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/macros.h"

namespace component {

class CacheControl : public fuchsia::sys::test::CacheControl {
 public:
  CacheControl();
  ~CacheControl() override;

  void AddBinding(fidl::InterfaceRequest<fuchsia::sys::test::CacheControl> request);

  //
  // fuchsia::sys::test::CacheControl implementation:
  //
  void Clear(ClearCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::sys::test::CacheControl> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CacheControl);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_CACHE_CONTROL_H_
