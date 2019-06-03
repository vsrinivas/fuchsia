// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_INTELLIGENCE_SERVICES_IMPL_H_
#define PERIDOT_BIN_SESSIONMGR_INTELLIGENCE_SERVICES_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>

namespace modular {

class IntelligenceServicesImpl : public fuchsia::modular::IntelligenceServices {
 public:
  // |context_engine| is not owned and must outlive this instance.
  IntelligenceServicesImpl(fuchsia::modular::ComponentScope scope,
                           fuchsia::modular::ContextEngine* context_engine);

  void GetContextReader(
      fidl::InterfaceRequest<fuchsia::modular::ContextReader> request) override;
  void GetContextWriter(
      fidl::InterfaceRequest<fuchsia::modular::ContextWriter> request) override;

 private:
  fuchsia::modular::ComponentScope CloneScope();

  fuchsia::modular::ComponentScope scope_;
  fuchsia::modular::ContextEngine* const context_engine_;  // Not owned.
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_INTELLIGENCE_SERVICES_IMPL_H_
