// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/applets/test_applets/test_applets.h"

#include <lib/syslog/cpp/macros.h>

namespace {

TestAppletSpec g_applet;
uint32_t g_instance_count = 0;

class TestApplet {
 public:
  TestApplet(FuchsiaWeaveAppletsCallbacksV1 callbacks) {
    auto applet_spec = &g_applet;
    FX_LOGS(ERROR) << "Creating test applet - counts: " << applet_spec->trait_sources.count;
    for (size_t i = 0; i < applet_spec->trait_sources.count; i++) {
      callbacks.publish_trait({}, 0, applet_spec->trait_sources.traits[i]);
    }
    for (size_t i = 0; i < applet_spec->trait_sinks.count; i++) {
      callbacks.subscribe_trait({}, 0, {}, applet_spec->trait_sinks.traits[i]);
    }
  }
};

fuchsia_weave_applets_handle_t create_applet(FuchsiaWeaveAppletsCallbacksV1 callbacks) {
  g_instance_count = 1;
  return reinterpret_cast<fuchsia_weave_applets_handle_t>(new TestApplet(callbacks));
}

bool delete_applet(fuchsia_weave_applets_handle_t applets_handle) {
  if (applets_handle == FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE) {
    return false;
  }
  g_instance_count = 0;
  delete reinterpret_cast<TestApplet*>(applets_handle);
  return true;
}

zx_status_t ext_set_applet(TestAppletSpec applet) {
  if (g_instance_count > 0) {
    return ZX_ERR_BAD_STATE;
  }
  g_applet = applet;
  return ZX_OK;
}

uint32_t ext_num_instances() { return g_instance_count; }

}  // namespace

DECLARE_FUCHSIA_WEAVE_APPLETS_MODULE_V1{
    &create_applet,
    &delete_applet,
};

DECLARE_TEST_APPLETS_EXT{
    &ext_set_applet,
    &ext_num_instances,
};
