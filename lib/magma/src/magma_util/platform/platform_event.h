// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_EVENT_H
#define PLATFORM_EVENT_H

#include <memory>

namespace magma {

// PlatformEvent is a one-shot event: initial state is unsignaled;
// after signaling, state is forever signaled.
class PlatformEvent {
public:
    static std::unique_ptr<PlatformEvent> Create();

    virtual ~PlatformEvent() {}

    virtual void Signal() = 0;
    virtual void Wait() = 0;
};

} // namespace magma

#endif // PLATFORM_EVENT_H
