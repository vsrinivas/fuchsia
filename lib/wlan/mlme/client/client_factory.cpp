// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/client_factory.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/timer_manager.h>

namespace wlan {

fbl::unique_ptr<ClientInterface> CreateDefaultClient(DeviceInterface* device, JoinContext* join_ctx,
                                                     ChannelScheduler* chan_scheduler) {
    fbl::unique_ptr<Timer> timer;
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kStation));
    timer_id.set_mac(join_ctx->bssid().ToU64());
    auto status = device->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
    if (status != ZX_OK) {
        errorf("could not create STA timer: %d\n", status);
        return nullptr;
    }
    return fbl::make_unique<Station>(device, TimerManager<>(std::move(timer)), chan_scheduler,
                                     join_ctx);
}

}  // namespace wlan
