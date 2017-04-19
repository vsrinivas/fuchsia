// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adapter_fidl_impl.h"

#include "lib/ftl/logging.h"

#include "fidl_helpers.h"

namespace bluetooth_service {

AdapterFidlImpl::AdapterFidlImpl(ftl::RefPtr<::bluetooth::gap::Adapter> adapter,
                                 ::fidl::InterfaceRequest<::bluetooth::control::Adapter> request,
                                 const ConnectionErrorHandler& connection_error_handler)
    : adapter_(adapter), binding_(this, std::move(request)) {
  FTL_DCHECK(adapter_);
  FTL_DCHECK(connection_error_handler);
  binding_.set_connection_error_handler(
      [this, connection_error_handler] { connection_error_handler(this); });
}

void AdapterFidlImpl::GetInfo(const GetInfoCallback& callback) {
  callback(fidl_helpers::NewAdapterInfo(*adapter_));
}

void AdapterFidlImpl::SetDelegate(
    ::fidl::InterfaceHandle<bluetooth::control::AdapterDelegate> delegate) {
  FTL_NOTIMPLEMENTED();
}

void AdapterFidlImpl::SetLocalName(const ::fidl::String& local_name,
                                   const ::fidl::String& shortened_local_name,
                                   const SetLocalNameCallback& callback) {
  FTL_NOTIMPLEMENTED();
}

void AdapterFidlImpl::SetPowered(bool powered, const SetPoweredCallback& callback) {
  FTL_NOTIMPLEMENTED();
}

}  // namespace bluetooth_service
