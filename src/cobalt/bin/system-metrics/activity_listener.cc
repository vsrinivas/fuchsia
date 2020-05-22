// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/activity_listener.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace cobalt {

fidl::InterfaceHandle<fuchsia::ui::activity::Listener> ActivityListener::NewHandle(
    async_dispatcher_t* dispatcher) {
  return binding_.NewBinding(dispatcher);
}

void ActivityListener::OnStateChanged(fuchsia::ui::activity::State state, zx_time_t transition_time,
                                      OnStateChangedCallback callback) {
  state_update_callback_(state);
  callback();
}

}  // namespace cobalt
