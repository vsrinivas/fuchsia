// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_APPLET_H_
#define SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_APPLET_H_

#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/weave/lib/applets/weave_applets.h"
#include "src/connectivity/weave/lib/applets_loader/applets_module.h"

namespace weavestack::applets {

class Applet {
 public:
  Applet() : applets_handle_(FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE) {}

  // Creates a new `Applet` from a `fuchsia_weave_applets_handle_t` and an owning
  // `AppletsModuleV1`.
  //
  // This constructor requires that both `handle` and `module` are both either valid or invalid
  // values. It is an error to create an `Applet` with `handle` ==
  // `FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE` while `module` is non-null. Likewise it is an error to
  // create an `Applet` with `handle` != `FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE` and a null
  // `module`.
  Applet(fuchsia_weave_applets_handle_t applets_handle, AppletsModuleV1 module)
      : applets_handle_(applets_handle), module_(std::move(module)) {
    // If handle_ is valid, module_ must be valid. If applets_handle_ is invalid, module_ must be
    // invalid.
    FX_DCHECK((applets_handle_ != FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE) == (module_.is_valid()));
  }

  ~Applet();

  // Allow move.
  Applet(Applet&& o) noexcept;
  Applet& operator=(Applet&& o) noexcept;

  // Returns |true| iff this Applet has a valid fuchsia_weave_applets_handle_t.
  [[nodiscard]] bool is_valid() const { return static_cast<bool>(module_); }
  explicit operator bool() const { return is_valid(); }

  [[nodiscard]] fuchsia_weave_applets_handle_t get() const { return applets_handle_; }

  // These methods are thin wrappers around the corresponding ABI calls that use the
  // fuchsia_weave_applets_handle_t and module used to create this applet. It is an error to call
  // any of these if the Applet instance is not valid (see |is_valid|).
  //
  // In the spirit of keeping these as thin wrappers around the fuchsia_weave_applets_handle_t,
  // this class will not perform any parameter checking; all arguments will be passed through to
  // the plugin as-is.

  // Creates the `Applet`, initializing the applet object.
  zx_status_t Create(FuchsiaWeaveAppletsCallbacksV1 callbacks);

  // Deletes the `Applet` leaving the object in an invalid state.
  //
  // Note that this will invalidate the `Applet` even if the operation fails.
  zx_status_t Delete();

 private:
  // Disallow copy.
  Applet(const Applet&) = delete;
  Applet& operator=(const Applet&) = delete;

  fuchsia_weave_applets_handle_t applets_handle_;
  AppletsModuleV1 module_;
};

}  // namespace weavestack::applets

#endif  // SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_APPLET_H_
