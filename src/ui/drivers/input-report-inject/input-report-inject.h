// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/intrusive_double_list.h>

#include "input-report-inject-instance.h"

#ifndef SRC_UI_DRIVERS_INPUT_REPORT_INJECT_INPUT_REPORT_INJECT_H_
#define SRC_UI_DRIVERS_INPUT_REPORT_INJECT_INPUT_REPORT_INJECT_H_

namespace input_report_inject {

class InputReportInject;
using DeviceType = ddk::Device<InputReportInject, ddk::UnbindableNew, ddk::Openable>;
class InputReportInject : public DeviceType,
                          public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT_INJECT> {
 public:
  InputReportInject(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~InputReportInject() = default;

  zx_status_t Bind();
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease() { delete this; }
  void RemoveInstanceFromList(InputReportInjectInstance* instance);

 private:
  fbl::Mutex instance_lock_;
  // Unmanaged linked-list because the HidInstances free themselves through DdkRelease.
  fbl::DoublyLinkedList<InputReportInjectInstance*> instance_list_ __TA_GUARDED(instance_lock_);
};

}  // namespace input_report_inject

#endif  // SRC_UI_DRIVERS_INPUT_REPORT_INJECT_INPUT_REPORT_INJECT_H_
