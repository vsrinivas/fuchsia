// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_APPLETS_TEST_APPLETS_TEST_APPLETS_H_
#define SRC_CONNECTIVITY_WEAVE_APPLETS_TEST_APPLETS_TEST_APPLETS_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "src/connectivity/weave/lib/applets/weave_applets.h"

__BEGIN_CDECLS

struct TestAppletSpec {
  struct {
    nl::Weave::Profiles::DataManagement_Current::TraitDataSource** traits;
    size_t count;
  } trait_sources;
  struct {
    nl::Weave::Profiles::DataManagement_Current::TraitDataSink** traits;
    size_t count;
  } trait_sinks;
};

// |TestAppletsModuleExt| is an extension interface that can be used to configure the behavior
// of the |test_applets| module. By interacting with this interface, tests can configure the
// behavior of this applet module.
struct TestAppletsModuleExt {
  // Creates the applet for the library. Must be called while the number of active applet
  // instances is zero. Only one applet is associated with this interface.
  zx_status_t (*set_applet)(TestAppletSpec applet);

  // Returns the number of active applet instances owned by this module.
  uint32_t (*num_instances)();
};

#define DECLARE_TEST_APPLETS_EXT __EXPORT TestAppletsModuleExt TestAppletsModuleExt_instance

__END_CDECLS

#endif  // SRC_CONNECTIVITY_WEAVE_APPLETS_TEST_APPLETS_TEST_APPLETS_H_
