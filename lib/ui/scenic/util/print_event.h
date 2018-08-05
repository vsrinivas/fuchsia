// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_UTIL_PRINT_EVENT_H_
#define GARNET_LIB_UI_SCENIC_UTIL_PRINT_EVENT_H_

#include <ostream>

#include <fuchsia/ui/gfx/cpp/fidl.h>

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::Event& event);

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::MetricsEvent& event);

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::ImportUnboundEvent& event);

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::ViewConnectedEvent& event);

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::ViewDisconnectedEvent& event);

std::ostream& operator<<(
    std::ostream& stream,
    const fuchsia::ui::gfx::ViewHolderDisconnectedEvent& event);

std::ostream& operator<<(
    std::ostream& stream,
    const fuchsia::ui::gfx::ViewAttachedToSceneEvent& event);

std::ostream& operator<<(
    std::ostream& stream,
    const fuchsia::ui::gfx::ViewDetachedFromSceneEvent& event);

std::ostream& operator<<(
    std::ostream& stream,
    const fuchsia::ui::gfx::ViewPropertiesChangedEvent& event);

#endif  // GARNET_LIB_UI_SCENIC_UTIL_PRINT_EVENT_H_
