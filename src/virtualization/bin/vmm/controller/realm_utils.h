// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_REALM_UTILS_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_REALM_UTILS_H_

#include <fuchsia/component/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

// Constructs a dynamic child component in CFv2 collection
// See https://fuchsia.dev/fuchsia-src/concepts/components/v2/realms#dynamic-children
//
// Roughly equivalent of fuchsia::sys::Launcher::CreateComponent.
// Callback passes services of the newly created child to allow caller specialized bind code while
// keeping the dynamic child creation logic in one place. |collection_name| is the name defined in
// the parent's cml file. |component_name| is an alphanumerical name of the child component to be
// created, unique within the parent component. |component_url| is the path to the child component
// cml file e.g. fuchsia-pkg://fuchsia.com/virtio_balloon#meta/virtio_balloon.cm
zx_status_t CreateDynamicComponent(
    fuchsia::component::RealmSyncPtr& realm, const char* collection_name,
    const char* component_name, const char* component_url,
    fit::function<zx_status_t(std::shared_ptr<sys::ServiceDirectory> services)> callback);

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_REALM_UTILS_H_
