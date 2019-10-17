// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_TRACE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_TRACE_H_

//
// SPINEL TRACING MACROS
//

//////////////////////////////////
//
// SUBALLOCATOR
//

// declares number of subbufs and bytes in a suballocator instance
#define SPN_TRACE_SUBALLOCATOR_CREATE(name_, instance_, subbufs_, bytes_)

// unique id tracks the alloc and free
#define SPN_TRACE_SUBALLOCATOR_ALLOC(instance_, id_, size_)
#define SPN_TRACE_SUBALLOCATOR_FREE(instance, id_, size_)

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_TRACE_H_

//
//
//
