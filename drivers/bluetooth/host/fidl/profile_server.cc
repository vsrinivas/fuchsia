// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "profile_server.h"

#include "helpers.h"

using fuchsia::bluetooth::ErrorCode;
using fuchsia::bluetooth::Status;

namespace fidlbredr = fuchsia::bluetooth::bredr;
using fidlbredr::Profile;

namespace bthost {

ProfileServer::ProfileServer(fxl::WeakPtr<::btlib::gap::Adapter> adapter,
                             fidl::InterfaceRequest<Profile> request)
    : AdapterServerBase(adapter, this, std::move(request)),
      weak_ptr_factory_(this) {}

ProfileServer::~ProfileServer() {}

void ProfileServer::AddService(fidlbredr::ServiceDefinition definition,
                               fidlbredr::SecurityLevel sec_level, bool devices,
                               AddServiceCallback callback) {
  // TODO: implement
  callback(fidl_helpers::NewFidlError(ErrorCode::NOT_SUPPORTED,
                                      "Not implemented yet."),
           "");
}

void ProfileServer::DisconnectClient(::fidl::StringPtr device_id,
                                     ::fidl::StringPtr service_id) {
  // TODO: implement
}

void ProfileServer::RemoveService(::fidl::StringPtr service_id) {
  // TODO: implement
}

}  // namespace bthost
