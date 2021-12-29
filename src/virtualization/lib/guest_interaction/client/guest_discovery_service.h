// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_DISCOVERY_SERVICE_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_DISCOVERY_SERVICE_H_

#include <fuchsia/netemul/guest/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>
#include <lib/sys/cpp/component_context.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "src/lib/fxl/macros.h"
#include "src/virtualization/lib/guest_interaction/client/guest_interaction_service.h"

struct GuestInfo {
  uint32_t realm_id;
  uint32_t guest_cid;

  bool operator==(const GuestInfo& rhs) const {
    return (realm_id == rhs.realm_id) && (guest_cid == rhs.guest_cid);
  }
};

template <>
struct std::hash<GuestInfo> {
  size_t operator()(GuestInfo const& guest_info) const noexcept {
    size_t result = 0;
    result = (result << 1) ^ std::hash<uint32_t>{}(guest_info.realm_id);
    result = (result << 1) ^ std::hash<uint32_t>{}(guest_info.guest_cid);
    return result;
  }
};

class GuestDiscoveryServiceImpl final : public fuchsia::netemul::guest::GuestDiscovery {
 public:
  explicit GuestDiscoveryServiceImpl(async_dispatcher_t*);
  void GetGuest(fidl::StringPtr realm_name, std::string guest_name,
                fidl::InterfaceRequest<fuchsia::netemul::guest::GuestInteraction>) override;

 private:
  using GuestCompleters =
      std::vector<fpromise::completer<FuchsiaGuestInteractionService*, zx_status_t>>;
  std::unordered_map<GuestInfo, std::variant<GuestCompleters, FuchsiaGuestInteractionService>>
      guests_;
  sys::ComponentContext context_;
  async::Executor executor_;
  fpromise::scope scope_;
  fuchsia::virtualization::ManagerPtr manager_;
  std::mutex lock_;
  std::unordered_set<std::shared_ptr<fpromise::completer<GuestInfo, zx_status_t>>>
      manager_completers_ __TA_GUARDED(lock_);
  fidl::BindingSet<fuchsia::netemul::guest::GuestDiscovery> bindings_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(GuestDiscoveryServiceImpl);
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_DISCOVERY_SERVICE_H_
