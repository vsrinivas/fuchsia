// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEBUG_DUMP_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEBUG_DUMP_H_

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <lib/zx/vmo.h>

#include <driver-info/driver-info.h>

#include "src/devices/bin/driver_manager/coordinator.h"
#include "src/devices/bin/driver_manager/vmo_writer.h"

class Coordinator;

class DebugDump : public fidl::WireServer<fuchsia_device_manager::DebugDumper> {
 public:
  DebugDump(const DebugDump&) = delete;
  DebugDump& operator=(const DebugDump&) = delete;
  DebugDump(DebugDump&&) = delete;
  DebugDump& operator=(DebugDump&&) = delete;

  explicit DebugDump(Coordinator* coordinator);
  ~DebugDump();

  // fuchsia.device.manager/DebugDump interface
  void DumpTree(DumpTreeRequestView request, DumpTreeCompleter::Sync& completer) override;
  void DumpDrivers(DumpDriversRequestView request, DumpDriversCompleter::Sync& completer) override;
  void DumpBindingProperties(DumpBindingPropertiesRequestView request,
                             DumpBindingPropertiesCompleter::Sync& completer) override;

  // Public for testing only.
  void DumpState(VmoWriter* vmo) const;

 private:
  // Owner. |coordinator_| must outlive DebugDump.
  Coordinator* coordinator_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEBUG_DUMP_H_
