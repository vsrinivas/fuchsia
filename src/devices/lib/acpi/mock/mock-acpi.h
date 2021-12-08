// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_LIB_ACPI_MOCK_MOCK_ACPI_H_
#define SRC_DEVICES_LIB_ACPI_MOCK_MOCK_ACPI_H_

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>

#include <functional>

#include "src/devices/lib/acpi/client.h"

namespace acpi::mock {

class Device : public fidl::WireServer<fuchsia_hardware_acpi::Device> {
 public:
  // Create an |acpi::Client| that will talk to this |Device|.
  zx::status<acpi::Client> CreateClient(async_dispatcher_t *dispatcher);

  // MOCK_FN_IMPL(name, default_error) creates:
  // * a |name|Fn type that is a callback.
  // * a Set|name| function that takes a |name|Fn and sets the callback for |name|.
  // * a |name| function that is the FIDL method implementation. By default it will
  // ReplyError(|default_error|), but if Set|name| has been called, it will call that callback.
  // * a |name|_fn_ member that contains the callback.
#define MOCK_FN_IMPL(name, default_error)                                           \
 public:                                                                            \
  using name##Fn = std::function<void(name##RequestView, name##Completer::Sync &)>; \
  void name(name##RequestView request, name##Completer::Sync &completer) override { \
    if (name##_fn_ == nullptr) {                                                    \
      completer.ReplyError(default_error);                                          \
    } else {                                                                        \
      name##_fn_(request, completer);                                               \
    }                                                                               \
  }                                                                                 \
  void Set##name(name##Fn fn) { name##_fn_ = std::move(fn); }                       \
                                                                                    \
 private:                                                                           \
  name##Fn name##_fn_ = nullptr /* deliberate missing semicolon */

  MOCK_FN_IMPL(GetBusId, ZX_ERR_NOT_SUPPORTED);
  MOCK_FN_IMPL(EvaluateObject, fuchsia_hardware_acpi::wire::Status::kNotImplemented);
  MOCK_FN_IMPL(MapInterrupt, ZX_ERR_NOT_SUPPORTED);
  MOCK_FN_IMPL(GetPio, ZX_ERR_NOT_SUPPORTED);
  MOCK_FN_IMPL(GetMmio, ZX_ERR_NOT_SUPPORTED);
  MOCK_FN_IMPL(InstallNotifyHandler, fuchsia_hardware_acpi::wire::Status::kNotImplemented);
  MOCK_FN_IMPL(AcquireGlobalLock, fuchsia_hardware_acpi::wire::Status::kNotImplemented);

#undef MOCK_FN_IMPL
};

}  // namespace acpi::mock
#endif  // SRC_DEVICES_LIB_ACPI_MOCK_MOCK_ACPI_H_
