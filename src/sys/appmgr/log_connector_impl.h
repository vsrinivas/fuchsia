// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_LOG_CONNECTOR_IMPL_H_
#define SRC_SYS_APPMGR_LOG_CONNECTOR_IMPL_H_

#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/stdcompat/optional.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace component {

class LogConnectorImpl : public fuchsia::sys::internal::LogConnector,
                         public fbl::RefCounted<LogConnectorImpl> {
 public:
  using TakeLogConnectionListenerCallback =
      fit::function<void(fidl::InterfaceRequest<fuchsia::sys::internal::LogConnectionListener>)>;

  // Construct a new connector for the provided realm label. This connector has no parent.
  LogConnectorImpl(std::string realm_label);

  // Construct a new connector for a child realm.
  fbl::RefPtr<LogConnectorImpl> NewChild(std::string child_realm_label);

  void AddConnectorClient(fidl::InterfaceRequest<fuchsia::sys::internal::LogConnector> request);

  // Adds a new LogSink connection from a running component. This LogSink connection is forwarded,
  // with attribution, to the LogConnectionListener.
  void AddLogConnection(std::string component_url, std::string instance_id,
                        fidl::InterfaceRequest<fuchsia::logger::LogSink> connection);

  void OnReady(fit::function<void()> on_ready);

 private:
  // Construct a new connector for the provided realm label.
  LogConnectorImpl(fxl::WeakPtr<LogConnectorImpl> parent, std::string realm_label);

  // |fuchsia::sys::LogConnector|.
  virtual void TakeLogConnectionListener(TakeLogConnectionListenerCallback cb) override;

  fxl::WeakPtr<LogConnectorImpl> parent_;
  std::string realm_label_;
  fidl::BindingSet<fuchsia::sys::internal::LogConnector> bindings_;
  fuchsia::sys::internal::LogConnectionListenerPtr consumer_;
  fidl::InterfaceRequest<fuchsia::sys::internal::LogConnectionListener> consumer_request_;

  // weak_factory_ must be the last member variable.
  fxl::WeakPtrFactory<LogConnectorImpl> weak_factory_;

  cpp17::optional<fit::function<void()>> on_ready_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogConnectorImpl);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_LOG_CONNECTOR_IMPL_H_
