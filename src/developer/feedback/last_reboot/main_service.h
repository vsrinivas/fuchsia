// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_REBOOT_INFO_MAIN_SERVICE_H_
#define SRC_DEVELOPER_FEEDBACK_REBOOT_INFO_MAIN_SERVICE_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/developer/feedback/last_reboot/last_reboot_info_provider.h"

namespace feedback {

class MainService {
 public:
  MainService(const RebootLog& reboot_log);

  // fuchsia.feedback.LastRebootInfoProvider
  void HandleLastRebootInfoProviderRequest(
      ::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request);

 private:
  LastRebootInfoProvider last_reboot_info_provider_;
  ::fidl::BindingSet<fuchsia::feedback::LastRebootInfoProvider>
      last_reboot_info_provider_connections_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_REBOOT_INFO_MAIN_SERVICE_H_
