// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//

#ifndef __CAPSULE_H
#define __CAPSULE_H

#include <stdint.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

// capsules are small chunks of data that survive short power events.
// They are basically tagged blobs. The capacity is dependent on the
// technology used.
//

// In the functions below a return value less than zero is an error
// from this set:
#define CAP_ERR_NOT_FOUND     -1
#define CAP_ERR_PARAMS        -2
#define CAP_ERR_SIZE          -3
#define CAP_ERR_BAD_HEADER    -4
#define CAP_ERR_CHECKSUM      -5

// Pass 0 in |capsule| and |size| in  to learn the supported capacitiy in
// the return value.
int32_t capsule_store(uint8_t tag, void* capsule, uint32_t size);

int32_t capsule_fetch(uint8_t tag, void* capsule, uint32_t size);

__END_CDECLS;

#endif
