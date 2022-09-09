// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_
#define SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_

#include <fidl/fuchsia.driver.host/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/driver2/record.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/zx/status.h>
#include <zircon/compiler.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>

#include "src/devices/bin/driver_host2/driver.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace dfv2 {

class DriverHost : public fidl::Server<fuchsia_driver_host::DriverHost> {
 public:
  // DriverHost does not take ownership of `loop`, and `loop` must outlive
  // DriverHost.
  DriverHost(inspect::Inspector& inspector, async::Loop& loop);

  fpromise::promise<inspect::Inspector> Inspect();
  zx::status<> PublishDriverHost(component::OutgoingDirectory& outgoing_directory);

 private:
  // fidl::Server<fuchsia_driver_host::DriverHost>
  void Start(StartRequest& request, StartCompleter::Sync& completer) override;

  void GetProcessKoid(GetProcessKoidRequest& request,
                      GetProcessKoidCompleter::Sync& completer) override;

  // Extracts the default_dispatcher_opts from |program| and converts it to
  // the options value expected by |fdf::Dispatcher::Create|.
  // Returns zero if no options were specified.
  uint32_t ExtractDefaultDispatcherOpts(const fuchsia_data::wire::Dictionary& program);

  async::Loop& loop_;
  std::mutex mutex_;
  fbl::DoublyLinkedList<fbl::RefPtr<Driver>> drivers_ __TA_GUARDED(mutex_);
};

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_
