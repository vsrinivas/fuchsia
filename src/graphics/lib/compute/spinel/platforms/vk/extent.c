// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "extent.h"

//
// DURABLE R/W HOST EXTENT -- STANDARD CACHED MEMORY
//

#if 0
void
spn_extent_phrw_alloc(struct spn_runtime     * const runtime,
                      struct spn_extent_phrw * const extent,
                      size_t                   const size)
{
  extent->hrw = spn_runtime_host_perm_alloc(runtime,SPN_MEM_FLAGS_READ_WRITE,size);
}

void
spn_extent_phrw_free(struct spn_runtime     * const runtime,
                     struct spn_extent_phrw * const extent)
{
  spn_runtime_host_perm_free(runtime,extent->hrw);
}
#endif

//
// DURABLE R/W DEVICE EXTENT -- ALLOCATED FROM DEVICE HEAP
//

void
spn_extent_pdrw_alloc(struct spn_extent_pdrw * const           extent,
                      struct spn_allocator_device_perm * const perm,
                      struct spn_vk_environment * const        vk,
                      uint64_t const                           size)
{
  spn_allocator_device_perm_alloc(perm, vk, size, NULL, &extent->dbi, &extent->devmem);
}

void
spn_extent_pdrw_free(struct spn_extent_pdrw * const           extent,
                     struct spn_allocator_device_perm * const perm,
                     struct spn_vk_environment * const        vk)
{
  spn_allocator_device_perm_free(perm, vk, &extent->dbi, extent->devmem);
}

//
// EPHEMERAL DEVICE R/W EXTENT -- ALLOCATED QUICKLY FROM A MANAGED RING
//

#if 0
void
spn_extent_tdrw_alloc(struct spn_runtime     * const runtime,
                      struct spn_extent_tdrw * const extent,
                      size_t                   const size)
{
  extent->size = size;
  extent->drw  = spn_runtime_device_temp_alloc(runtime,
                                               CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS,
                                               size,&extent->id,NULL);
}

void
spn_extent_tdrw_free(struct spn_runtime     * const runtime,
                     struct spn_extent_tdrw * const extent)
{
  spn_runtime_device_temp_free(runtime,extent->drw,extent->id);
}

void
spn_extent_tdrw_zero(struct spn_extent_tdrw * const extent,
                     cl_command_queue         const cq,
                     cl_event               * const event)
{
  if (extent->size == 0)
    return;

  spn_uint const zero = 0;

  cl(EnqueueFillBuffer(cq,
                       extent->drw,
                       &zero,
                       sizeof(zero),
                       0,
                       extent->size,
                       0,NULL,event));
}
#endif

//
// DURABLE SMALL EXTENTS BACKING ATOMICS
//

#if 0
void
spn_extent_phr_pdrw_alloc(struct spn_runtime         * const runtime,
                          struct spn_extent_phr_pdrw * const extent,
                          size_t                       const size)
{
  extent->size = size;
  extent->hr   = spn_runtime_host_perm_alloc(runtime,SPN_MEM_FLAGS_READ_ONLY,size);
  extent->drw  = spn_runtime_device_perm_alloc(runtime,CL_MEM_READ_WRITE,size);
}

void
spn_extent_phr_pdrw_free(struct spn_runtime         * const runtime,
                         struct spn_extent_phr_pdrw * const extent)
{
  spn_runtime_host_perm_free(runtime,extent->hr);
  spn_runtime_device_perm_free(runtime,extent->drw);
}

void
spn_extent_phr_pdrw_read(struct spn_extent_phr_pdrw * const extent,
                         cl_command_queue             const cq,
                         cl_event                   * const event)
{
  if (extent->size == 0)
    return;

  cl(EnqueueReadBuffer(cq,
                       extent->drw,
                       CL_FALSE,
                       0,
                       extent->size,
                       extent->hr,
                       0,NULL,event));
}

void
spn_extent_phr_pdrw_zero(struct spn_extent_phr_pdrw * const extent,
                         cl_command_queue             const cq,
                         cl_event                   * const event)
{
  if (extent->size == 0)
    return;

  spn_uint const zero = 0;

  cl(EnqueueFillBuffer(cq,
                       extent->drw,
                       &zero,
                       sizeof(zero),
                       0,
                       extent->size,
                       0,NULL,event));
}
#endif

//
// EPHEMERAL SMALL EXTENTS BACKING ATOMICS
//

#if 0
void
spn_extent_thr_tdrw_alloc(struct spn_runtime         * const runtime,
                          struct spn_extent_thr_tdrw * const extent,
                          size_t                       const size)
{
  extent->size = size;
  extent->hr   = spn_runtime_host_temp_alloc(runtime,
                                             SPN_MEM_FLAGS_READ_ONLY,
                                             size,&extent->id.hr,NULL);
  extent->drw  = spn_runtime_device_temp_alloc(runtime,
                                               CL_MEM_READ_WRITE,
                                               size,
                                               &extent->id.drw,
                                               NULL);
}

void
spn_extent_thr_tdrw_free(struct spn_runtime         * const runtime,
                         struct spn_extent_thr_tdrw * const extent)
{
  spn_runtime_host_temp_free(runtime,extent->hr,extent->id.hr);
  spn_runtime_device_temp_free(runtime,extent->drw,extent->id.drw);
}

void
spn_extent_thr_tdrw_read(struct spn_extent_thr_tdrw * const extent,
                         cl_command_queue             const cq,
                         cl_event                   * const event)
{
  if (extent->size == 0)
    return;

  cl(EnqueueReadBuffer(cq,
                       extent->drw,
                       CL_FALSE,
                       0,
                       extent->size,
                       extent->hr,
                       0,NULL,event));
}

void
spn_extent_thr_tdrw_zero(struct spn_extent_thr_tdrw * const extent,
                         cl_command_queue             const cq,
                         cl_event                   * const event)
{
  if (extent->size == 0)
    return;

  spn_uint const zero = 0;

  cl(EnqueueFillBuffer(cq,
                       extent->drw,
                       &zero,
                       sizeof(zero),
                       0,
                       extent->size,
                       0,NULL,event));
}
#endif

//
// DURABLE W/1 HOST RING WITH AN EPHEMERAL R/N DEVICE SNAPSHOT
//

#if 0
void
spn_extent_phw1g_tdrNs_alloc(struct spn_runtime            * const runtime,
                             struct spn_extent_phw1g_tdrNs * const extent,
                             size_t                          const size)
{
  extent->hw1 = spn_runtime_host_perm_alloc(runtime,SPN_MEM_FLAGS_WRITE_ONLY,size);
}

void
spn_extent_phw1g_tdrNs_free(struct spn_runtime            * const runtime,
                            struct spn_extent_phw1g_tdrNs * const extent)
{
  spn_runtime_host_perm_free(runtime,extent->hw1);
}

void
spn_extent_phw1g_tdrNs_snap_init(struct spn_runtime                 * const runtime,
                                 struct spn_extent_ring             * const ring,
                                 struct spn_extent_phw1g_tdrNs_snap * const snap)
{
  snap->snap = spn_extent_ring_snap_alloc(runtime,ring);
}

void
spn_extent_phw1g_tdrNs_snap_alloc(struct spn_runtime                 * const runtime,
                                  struct spn_extent_phw1g_tdrNs      * const extent,
                                  struct spn_extent_phw1g_tdrNs_snap * const snap,
                                  cl_command_queue                     const cq,
                                  cl_event                           * const event)
{
  struct spn_extent_ring const * const ring = snap->snap->ring;

  spn_uint const count = spn_extent_ring_snap_count(snap->snap);
  size_t   const size  = count * ring->size.elem;

  snap->drN = spn_runtime_device_temp_alloc(runtime,
                                            CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY,
                                            size,&snap->id,NULL);

  if (count == 0)
    return;

  // possibly two copies
  spn_uint const index_lo  = snap->snap->reads & ring->size.mask;
  spn_uint const count_max = ring->size.pow2 - index_lo;
  spn_uint const count_lo  = min(count_max,count);
  size_t   const bytes_lo  = count_lo * ring->size.elem;

  if (count > count_max)
    {
      spn_uint const bytes_hi = (count - count_max) * ring->size.elem;

      cl(EnqueueWriteBuffer(cq,
                            snap->drN,
                            CL_FALSE,
                            bytes_lo,
                            bytes_hi,
                            extent->hw1, // offset_hi = 0
                            0,NULL,NULL));
    }

  size_t const offset_lo = index_lo * ring->size.elem;

  cl(EnqueueWriteBuffer(cq,
                        snap->drN,
                        CL_FALSE,
                        0,
                        bytes_lo,
                        (spn_uchar*)extent->hw1 + offset_lo,
                        0,NULL,event));

}

void
spn_extent_phw1g_tdrNs_snap_free(struct spn_runtime                 * const runtime,
                                 struct spn_extent_phw1g_tdrNs_snap * const snap)
{
  spn_runtime_device_temp_free(runtime,snap->drN,snap->id);
  spn_extent_ring_snap_free(runtime,snap->snap);
}

//
// DURABLE R/W HOST RING WITH AN EPHEMERAL R/N DEVICE SNAPSHOT
//

void
spn_extent_phrwg_tdrNs_alloc(struct spn_runtime            * const runtime,
                             struct spn_extent_phrwg_tdrNs * const extent,
                             size_t                          const size)
{
  extent->hrw = spn_runtime_host_perm_alloc(runtime,SPN_MEM_FLAGS_READ_WRITE,size); // WRITE-ONCE
}

void
spn_extent_phrwg_tdrNs_free(struct spn_runtime            * const runtime,
                            struct spn_extent_phrwg_tdrNs * const extent)
{
  spn_runtime_host_perm_free(runtime,extent->hrw);
}

void
spn_extent_phrwg_tdrNs_snap_init(struct spn_runtime                 * const runtime,
                                 struct spn_extent_ring             * const ring,
                                 struct spn_extent_phrwg_tdrNs_snap * const snap)
{
  snap->snap = spn_extent_ring_snap_alloc(runtime,ring);
}

void
spn_extent_phrwg_tdrNs_snap_alloc(struct spn_runtime                 * const runtime,
                                  struct spn_extent_phrwg_tdrNs      * const extent,
                                  struct spn_extent_phrwg_tdrNs_snap * const snap,
                                  cl_command_queue                     const cq,
                                  cl_event                           * const event)
{
  struct spn_extent_ring const * const ring = snap->snap->ring;

  spn_uint const count = spn_extent_ring_snap_count(snap->snap);
  size_t   const size  = count * ring->size.elem;

  snap->drN = spn_runtime_device_temp_alloc(runtime,
                                            CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY,
                                            size,&snap->id,NULL);

  if (count == 0)
    return;

  // possibly two copies
  spn_uint const index_lo  = snap->snap->reads & ring->size.mask;
  spn_uint const count_max = ring->size.pow2 - index_lo;
  spn_uint const count_lo  = min(count_max,count);
  size_t   const bytes_lo  = count_lo * ring->size.elem;

  if (count > count_max)
    {
      spn_uint const count_hi = count - count_max;
      spn_uint const bytes_hi = count_hi * ring->size.elem;

      cl(EnqueueWriteBuffer(cq,
                            snap->drN,
                            CL_FALSE,
                            bytes_lo,
                            bytes_hi,
                            extent->hrw, // offset_hi = 0
                            0,NULL,NULL));
    }

  size_t offset_lo = index_lo * ring->size.elem;

  cl(EnqueueWriteBuffer(cq,
                        snap->drN,
                        CL_FALSE,
                        0,
                        bytes_lo,
                        (spn_uchar*)extent->hrw + offset_lo,
                        0,NULL,event));

}

void
spn_extent_phrwg_tdrNs_snap_free(struct spn_runtime                 * const runtime,
                                 struct spn_extent_phrwg_tdrNs_snap * const snap)
{
  spn_runtime_device_temp_free(runtime,snap->drN,snap->id);
  spn_extent_ring_snap_free(runtime,snap->snap);
}
#endif

//
// DURABLE HOST R/W RING WITH AN EPHEMERAL HOST R/1 SNAPSHOT
//
// Note that because the ring and snapshot are both in host memory and
// the snapshot blocks progress until freed we can simply point the
// fake ephemeral snapshot at the ring's durable extent.
//

#if 0
void
spn_extent_phrwg_thr1s_alloc(struct spn_runtime            * const runtime,
                             struct spn_extent_phrwg_thr1s * const extent,
                             size_t                          const size)
{
  extent->hrw = spn_runtime_host_perm_alloc(runtime,SPN_MEM_FLAGS_READ_WRITE,size); // WRITE-ONCE
}

void
spn_extent_phrwg_thr1s_free(struct spn_runtime            * const runtime,
                            struct spn_extent_phrwg_thr1s * const extent)
{
  spn_runtime_host_perm_free(runtime,extent->hrw);
}

void
spn_extent_phrwg_thr1s_snap_init(struct spn_runtime                 * const runtime,
                                 struct spn_extent_ring             * const ring,
                                 struct spn_extent_phrwg_thr1s_snap * const snap)
{
  snap->snap = spn_extent_ring_snap_alloc(runtime,ring);
}

void
spn_extent_phrwg_thr1s_snap_alloc(struct spn_runtime                 * const runtime,
                                  struct spn_extent_phrwg_thr1s      * const extent,
                                  struct spn_extent_phrwg_thr1s_snap * const snap)
{
  struct spn_extent_ring const * const ring = snap->snap->ring;

  spn_uint const count     = spn_extent_ring_snap_count(snap->snap);
  spn_uint const index_lo  = snap->snap->reads & ring->size.mask;
  spn_uint const count_max = ring->size.pow2 - index_lo;

  snap->count.lo = min(count_max,count);
  snap->hr1.lo   = (spn_uchar*)extent->hrw + (index_lo * ring->size.elem);

  if (count > count_max)
    {
      snap->count.hi = count - count_max;
      snap->hr1.hi   = extent->hrw;
    }
  else
    {
      snap->count.hi = 0;
      snap->hr1.hi   = NULL;
    }
}

void
spn_extent_phrwg_thr1s_snap_free(struct spn_runtime                 * const runtime,
                                 struct spn_extent_phrwg_thr1s_snap * const snap)
{
  spn_extent_ring_snap_free(runtime,snap->snap);
}
#endif

//
//
//
