// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <err.h>
#include <sys/types.h>

__BEGIN_CDECLS

typedef void* interrupt_event_t;

/* Interrupt events are raised by hardware and can be waited upon by one or more threads.
 * Rules:
 * - An interrupt is unmasked when create_interrupt_event() is called for it for the first time
 * - All threads that are waiting are woken up when an interrupt is raised.
 * - Each thread must call interrupt_event_complete() before waiting again.
 * - The interrupt is masked until all woken threads call interrupt_event_complete().
 */

// Creates an interrupt event if there is none for this vector.
#define INTERRUPT_EVENT_FLAG_REMAP_IRQ (0x1)
status_t interrupt_event_create(unsigned int vector, uint32_t flags, interrupt_event_t* ie);

// Waits for an interrupt event. If there is a pending interrupt, this returns immediately.
// Otherwise, the thread is blocked until the next interrupt.
status_t interrupt_event_wait(interrupt_event_t ie);

// Notifies the kernel that the interrupt has been processed by the calling thread.
void interrupt_event_complete(interrupt_event_t ie);

__END_CDECLS
