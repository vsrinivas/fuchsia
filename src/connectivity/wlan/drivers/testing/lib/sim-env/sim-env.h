// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The simulated environment of physical world.
//
// In order to support unit-test of the driver code, we need to mimic the
// real-world. See the below diagram, the 'sim-env' module accepts the requests
// from 'unit test' to create a virtual environment and interacts with
// the 'sim-fw', which is a firmware simulation model.
//
//   +-------------+           +------------+
//   |  unit test  | <-------> |   driver   |
//   +-------------+           +------------+
//        ^     \                    ^
//        |        \                 |
//        |           \              |
//        |              \           |
//        v                 \        v
//   +-------------+           +------------+
//   |   sim-env   | <-------> |   sim-fw   |
//   +-------------+           +------------+
//
// In this framework, 'unit test', 'driver' and 'sim-fw' are device-specific
// implementations. 'sim-env' is commonly used for all drivers. Therefore we can
// leverage the fancy features (e.g. RSSI model) in the sim-env for all drivers.
//

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_ENV_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_ENV_H_

#include <fuchsia/hardware/wlan/associnfo/c/banjo.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>
#include <netinet/if_ether.h>
#include <stdint.h>
#include <zircon/types.h>

#include <list>
#include <map>
#include <mutex>

#include <wlan/common/ieee80211.h>
#include <wlan/common/macaddr.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-frame.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sig-loss-model.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"

namespace wlan::simulation {

// To simulate the physical environment.
//
class Environment {
 public:
  Environment();
  ~Environment();

  // Add a station into the environment.
  void AddStation(StationIfc* sta) { stations_.emplace(std::pair(sta, Location(0, 0))); }

  // Add a station into the environment at specific location.
  void AddStation(StationIfc* sta, int32_t x, int32_t y) {
    stations_.emplace(std::pair(sta, Location(x, y)));
  }

  // Remove a station from the environment.
  void RemoveStation(StationIfc* sta) { stations_.erase(sta); }

  // Change the location of a station in the environment.
  void MoveStation(StationIfc* sta, int32_t x, int32_t y) {
    RemoveStation(sta);
    stations_.emplace(std::pair(sta, Location(x, y)));
  }

  // Begin simulation for the given duration.
  void Run(zx::duration run_time_limit);

  // Send a frame into the simulated environment.
  void Tx(const SimFrame& frame, const WlanTxInfo& tx_info, StationIfc* sender);

  // Calculate frame transmission time.
  zx::duration CalcTransTime(StationIfc* staTx, StationIfc* staRx);

  // Schedule a future notification, at a duration `delay` past time `time_`.  Returns the ID of the
  // notification in `id_out` iff it is non-nullptr.
  zx_status_t ScheduleNotification(std::function<void()> handler, zx::duration delay,
                                   uint64_t* id_out = nullptr);

  // Cancel a future notification, return scheduled payload for station to handle
  zx_status_t CancelNotification(uint64_t id);

  // Get simulation absolute time
  zx::time GetTime() const;
  zx::time GetLatestEventTime() const;

  // The notification schedule/cancel API, as an async_dispatcher_t.
  zx_status_t PostTask(async_task_t* task);
  zx_status_t CancelTask(async_task_t* task);
  async_dispatcher_t* GetDispatcher();

 private:
  struct EnvironmentEvent {
    explicit EnvironmentEvent(std::function<void(zx_status_t)> fn, zx::time deadline, uint64_t id)
        : fn(std::move(fn)), deadline(deadline), id(id) {}

    // The event handler to fire.  Invoked with ZX_OK when the timer fires, or ZX_ERR_CANCELED if
    // the environment is being shut down before the event can fire.
    std::function<void(zx_status_t)> fn;

    // The absolute time at which to fire the event.
    zx::time deadline = {};

    // This event's ID.
    uint64_t id = 0;
  };

  struct Dispatcher : public async_dispatcher_t {
    Environment* parent = nullptr;
  };

  void HandleTxNotification(StationIfc* sta, std::shared_ptr<const SimFrame> frame,
                            std::shared_ptr<const WlanRxInfo> tx_info);

  // All registered stations
  std::map<StationIfc*, Location> stations_;

  // Signal strength loss model
  std::unique_ptr<SignalLossModel> signal_loss_model_;

  // Velocity of radio wave in meter/nanosecond
  static constexpr double kRadioWaveVelocity = 0.3;

  // Mutex for event-related state.
  mutable std::mutex event_mutex_;

  // Future events, sorted by time
  std::list<EnvironmentEvent> events_;

  // The next event ID to assign, starting from 1 (as 0 is invalid).
  uint64_t event_id_ = 1;

  // Current time
  zx::time time_ = {};

  zx::time latest_event_deadline_ = {};

  // Dispatcher instance.
  Dispatcher dispatcher_;
};

}  // namespace wlan::simulation

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_ENV_H_
