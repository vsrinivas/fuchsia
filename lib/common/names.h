// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_COMMON_NAMES_H_
#define PERIDOT_LIB_COMMON_NAMES_H_

namespace modular {

// A framework-assigned name for the first module of a story (aka root mod)
// created when using
// fuchsia::modular::StoryProvider::fuchsia::modular::CreateStory() or
// fuchsia::modular::StoryProvider::CreateStoryWithInfo() with a non-null
// |module_url| parameter.
constexpr char kRootModuleName[] = "root";

// The service name of the Presentation service that is routed between
// fuchsia::modular::DeviceShell and fuchsia::modular::UserShell. The same
// service exchage between fuchsia::modular::UserShell and
// fuchsia::modular::StoryShell uses the
// fuchsia::modular::UserShellPresentationProvider service, which is
// discoverable.
constexpr char kPresentationService[] = "mozart.Presentation";

}  // namespace modular

#endif  // PERIDOT_LIB_COMMON_NAMES_H_
