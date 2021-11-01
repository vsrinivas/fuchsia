// Copyright 2021 The Fuchsia Authors>
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>

#include <phys/exception.h>
#include <phys/main.h>

PhysHandledException gPhysHandledException;

// This is called from the vector code in exception.S for all exceptions.
extern "C" uint64_t PhysException(uint64_t vector, const char* vector_name,
                                  PhysExceptionState& state) {
  // Check for an installed handler, and always reset to no handler.
  PhysHandledException handled = gPhysHandledException;
  gPhysHandledException = {};

  // If the handler was expecting this PC to get an exception, it takes over.
  if (handled.pc != 0 && state.pc() == handled.pc) {
    return handled.handler(vector, vector_name, state);
  }

  // Otherwise complain verbosely and reboot.
  PrintPhysException(vector, vector_name, state);
  ArchPanicReset();
}
