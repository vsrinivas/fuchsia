// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// !!! DEPRECATED !!!
// New usages should reference sdk/lib/sys/cpp/...

#ifndef LIB_COMPONENT_CPP_ENVIRONMENT_SERVICES_HELPER_H_
#define LIB_COMPONENT_CPP_ENVIRONMENT_SERVICES_HELPER_H_

#include <lib/zx/channel.h>

#include <string>

#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/svc/cpp/services.h"

namespace component {

// Returns environment services handle which can be used to connect to services
// in components environment.
//
// This function is used by tests and setup functions to access services in
// underlying environment.
//
// Libraries should not use this function or copy its implementation as that
// will make code un-unit-testable.  The implementation of this function should
// only be used in prod code to setup component from main or equivalent so that
// component remains unit testable.
std::shared_ptr<component::Services> GetEnvironmentServices();

}  // namespace component

#endif  // LIB_COMPONENT_CPP_ENVIRONMENT_SERVICES_HELPER_H_
