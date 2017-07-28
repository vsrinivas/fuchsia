// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// TODO(johngro) : Remove this header once the API users have been updated.
#include "drivers/audio/dispatcher-pool/dispatcher-thread-pool.h"

namespace audio {

class DispatcherThread {
public:
    static void ShutdownThreadPool() {
        dispatcher::ThreadPool::ShutdownAll();
    }
};

}  // namespace audio
