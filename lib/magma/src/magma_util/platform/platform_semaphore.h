// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_SEMAPHORE_H
#define PLATFORM_SEMAPHORE_H

#include <memory>

#include "platform_object.h"

namespace magma {

class PlatformPort;

// Semantics of PlatformSemaphore match Vulkan semaphores. From:
//
// https://www.khronos.org/registry/vulkan/specs/1.0/html/vkspec.html#synchronization-semaphores
//
// "Semaphores are a synchronization primitive that can be used to insert a dependency between
// batches submitted to queues. Semaphores have two states - signaled and unsignaled. The state
// of a semaphore can be signaled after execution of a batch of commands is completed. A batch
// can wait for a semaphore to become signaled before it begins execution, and the semaphore is
// also unsignaled before the batch begins execution."
//
// "Unlike fences or events, the act of waiting for a semaphore also unsignals that semaphore.
// If two operations are separately specified to wait for the same semaphore, and there are no
// other execution dependencies between those operations, behaviour is undefined. An execution
// dependency must be present that guarantees that the semaphore unsignal operation for the first
// of those waits, happens-before the semaphore is signalled again, and before the second unsignal
// operation. Semaphore waits and signals should thus occur in discrete 1:1 pairs."
//
class PlatformSemaphore : public PlatformObject {
public:
    static std::unique_ptr<PlatformSemaphore> Create();

    static std::unique_ptr<PlatformSemaphore> Import(uint32_t handle);

    virtual ~PlatformSemaphore() {}

    // Signal the semaphore.  State must be unsignalled.
    // Called only by the driver device thread.
    virtual void Signal() = 0;

    // Resets the state to unsignalled. State may be signalled or unsignalled.
    // Called by the client (apps thread) and by the driver device thread.
    virtual void Reset() = 0;

    // Returns true if the event is signaled before the timeout expires, in which case
    // the state is reset to unsignalled.
    // Only one thread should ever wait on a given semaphore.
    virtual bool Wait(uint64_t timeout_ms) = 0;

    bool Wait() { return Wait(UINT64_MAX); }

    // Registers an async wait delivered on the given port when this semaphore is signalled.
    // Note that a port wait completion will not autoreset the semaphore.
    virtual bool WaitAsync(PlatformPort* platform_port) = 0;
};

} // namespace magma

#endif // PLATFORM_SEMAPHORE_H
