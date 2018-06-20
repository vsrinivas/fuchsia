// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_COMMON_NAMES_H_
#define PERIDOT_LIB_COMMON_NAMES_H_

namespace modular {

// A framework-assigned name for the first module of a story (aka root mod)
// created when using StoryProvider.CreateStory() or
// StoryProvider.CreateStoryWithInfo() with a non-null |module_url| parameter.
constexpr char kRootModuleName[] = "root";

// The service name of the Presentation service that is routed between
// DeviceShell and UserShell. The same service exchange between UserShell and
// StoryShell uses the UserShellPresentationProvider service, which is
// discoverable.
// TODO(SCN-595): mozart.Presentation is being renamed to ui.Presenter.
constexpr char kPresentationService[] = "mozart.Presentation";

}  // namespace modular

#endif  // PERIDOT_LIB_COMMON_NAMES_H_
