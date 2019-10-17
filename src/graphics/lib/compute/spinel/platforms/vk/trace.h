// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TRACE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TRACE_H_

//
// SPINEL/VK TRACING MACROS
//

//////////////////////////////////
//
// VK DESCRIPTOR SET POOLS
//

// declares size of a descriptor set pool
#define SPN_VK_TRACE_DS_POOL_CREATE(ds_name_, pool_size_)

// unique id tracks the acquire/release of one descriptor set
#define SPN_VK_TRACE_DS_POOL_ACQUIRE(ds_name_, id_)
#define SPN_VK_TRACE_DS_POOL_RELEASE(ds_name_, id_)

//////////////////////////////////
//
// VK PATH BUILDER DISPATCH RING
//

// declares initial number of dispatches in the path builder
#define SPN_VK_TRACE_PATH_BUILDER_CREATE(instance_, count_)

// traces the lifecycle of a path builder dispatch index
#define SPN_VK_TRACE_PATH_BUILDER_DISPATCH_ACQUIRE(instance_, dispatch_idx_)
#define SPN_VK_TRACE_PATH_BUILDER_DISPATCH_FLUSH(instance_, dispatch_idx_)
#define SPN_VK_TRACE_PATH_BUILDER_DISPATCH_RELEASE(instance_, dispatch_idx_)

//////////////////////////////////
//
// VK RASTER BUILDER DISPATCH RING
//

// -- multiple phases in the raster builder

//////////////////////////////////
//
// VK COMPOSITION LIFECYCLE
//

//////////////////////////////////
//
// VK STYLING LIFECYCLE
//

//////////////////////////////////
//
// VK RENDER SUBMIT/COMPLETE
//

//////////////////////////////////
//
// VK COMMAND BUFFER DISPATCH TIMELINE
//

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TRACE_H_

//
//
//
