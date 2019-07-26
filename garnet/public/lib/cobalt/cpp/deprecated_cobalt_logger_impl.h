// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_COBALT_CPP_DEPRECATED_COBALT_LOGGER_IMPL_H_
#define GARNET_PUBLIC_LIB_COBALT_CPP_DEPRECATED_COBALT_LOGGER_IMPL_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>

#include "src/lib/cobalt/cpp/cobalt_logger_impl.h"

namespace cobalt {

class DeprecatedCobaltLoggerImpl : public BaseCobaltLoggerImpl {
 public:
  DeprecatedCobaltLoggerImpl(async_dispatcher_t* dispatcher, component::StartupContext* context,
                             fuchsia::cobalt::ProjectProfile profile);

 protected:
  virtual fidl::InterfacePtr<fuchsia::cobalt::LoggerFactory> ConnectToLoggerFactory() override;

 private:
  component::StartupContext* component_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeprecatedCobaltLoggerImpl);
};

}  // namespace cobalt

#endif  // GARNET_PUBLIC_LIB_COBALT_CPP_DEPRECATED_COBALT_LOGGER_IMPL_H_
