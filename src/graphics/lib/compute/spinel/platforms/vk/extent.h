// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_EXTENT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_EXTENT_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "allocator_device.h"
#include "extent_ring.h"

//
//
//

struct spn_device;  // FIXME -- remove

//
// Legend:
//
//   p  :  durable
//   t  :  ephemeral
//   h  :  host
//   d  :  device
//   r  :  read
//   w  :  write
//   1  :  once -- e.g. w1 is 'write-once'
//   N  :  many -- e.g. rN is 'read-many'
//   g  :  ring
//   s  :  ring snapshot
//
// Notes:
//
//   rw :  for now, read-write implies read-write many
//

//
// DURABLE R/W HOST EXTENT -- STANDARD CACHED MEMORY
//

struct spn_extent_phrw
{
  void * hrw;
};

void
spn_extent_phrw_alloc(struct spn_extent_phrw * const extent,
                      struct spn_device * const      device,
                      size_t const                   size);

void
spn_extent_phrw_free(struct spn_extent_phrw * const extent, struct spn_device * const device);

//
// DURABLE R/W DEVICE EXTENT -- ALLOCATED FROM DEVICE HEAP
//

struct spn_extent_pdrw
{
  VkDescriptorBufferInfo dbi;
  VkDeviceMemory         devmem;
};

void
spn_extent_pdrw_alloc(struct spn_extent_pdrw * const           extent,
                      struct spn_allocator_device_perm * const perm,
                      struct spn_device_vk * const             vk,
                      uint64_t const                           size);

void
spn_extent_pdrw_free(struct spn_extent_pdrw * const           extent,
                     struct spn_allocator_device_perm * const perm,
                     struct spn_device_vk * const             vk);

//
// EPHEMERAL DEVICE R/W EXTENT -- ALLOCATED QUICKLY FROM A MANAGED RING
//

struct spn_extent_tdrw
{
  VkDeviceSize    size;
  VkBuffer        drw;
  spn_subbuf_id_t id;
};

void
spn_extent_tdrw_alloc(struct spn_extent_tdrw * const extent,
                      struct spn_device * const      device,
                      size_t const                   size);

void
spn_extent_tdrw_free(struct spn_extent_tdrw * const extent, struct spn_device * const device);

void
spn_extent_tdrw_zero(struct spn_extent_tdrw * const extent, VkCommandBuffer cb);

//
// DURABLE SMALL EXTENTS BACKING ATOMICS
//

struct spn_extent_phr_pdrw
{
  VkDeviceSize size;  // must be multiple of words
  void *       hr;
  VkBuffer     drw;
};

void
spn_extent_phr_pdrw_alloc(struct spn_extent_phr_pdrw * const extent,
                          struct spn_device * const          device,
                          size_t const                       size);

void
spn_extent_phr_pdrw_free(struct spn_extent_phr_pdrw * const extent,
                         struct spn_device * const          device);

void
spn_extent_phr_pdrw_read(struct spn_extent_phr_pdrw * const extent, VkCommandBuffer cb);

void
spn_extent_phr_pdrw_zero(struct spn_extent_phr_pdrw * const extent, VkCommandBuffer cb);

//
// EPHEMERAL SMALL EXTENTS BACKING ATOMICS
//

struct spn_extent_thr_tdrw
{
  VkDeviceSize size;  // must be multiple of words

  void *   hr;
  VkBuffer drw;

  struct
  {
    spn_subbuf_id_t hr;
    spn_subbuf_id_t drw;
  } id;
};

void
spn_extent_thr_tdrw_alloc(struct spn_extent_thr_tdrw * const extent,
                          struct spn_device * const          device,
                          size_t const                       size);

void
spn_extent_thr_tdrw_free(struct spn_extent_thr_tdrw * const extent,
                         struct spn_device * const          device);

void
spn_extent_thr_tdrw_read(struct spn_extent_thr_tdrw * const extent, VkCommandBuffer cb);

void
spn_extent_thr_tdrw_zero(struct spn_extent_thr_tdrw * const extent, VkCommandBuffer cb);

//
// DURABLE W/1 HOST RING WITH AN EPHEMERAL R/N DEVICE SNAPSHOT
//

struct spn_extent_phw1g_tdrNs
{
  void * hw1;
};

struct spn_extent_phw1g_tdrNs_snap
{
  struct spn_extent_ring_snap * snap;
  VkBuffer                      drN;
  spn_subbuf_id_t               id;
};

void
spn_extent_phw1g_tdrNs_alloc(struct spn_extent_phw1g_tdrNs * const extent,
                             struct spn_device * const             device,
                             size_t const                          size);

void
spn_extent_phw1g_tdrNs_free(struct spn_extent_phw1g_tdrNs * const extent,
                            struct spn_device * const             device);

void
spn_extent_phw1g_tdrNs_snap_init(struct spn_extent_phw1g_tdrNs_snap * const snap,
                                 struct spn_extent_ring * const             ring,
                                 struct spn_device * const                  device);

void
spn_extent_phw1g_tdrNs_snap_alloc(struct spn_extent_phw1g_tdrNs_snap * const snap,
                                  struct spn_extent_phw1g_tdrNs * const      extent,
                                  struct spn_device * const                  device,
                                  VkCommandBuffer                            cb);

void
spn_extent_phw1g_tdrNs_snap_free(struct spn_extent_phw1g_tdrNs_snap * const snap,
                                 struct spn_device * const                  device);

//
// DURABLE R/W HOST RING WITH AN EPHEMERAL R/N DEVICE SNAPSHOT
//

struct spn_extent_phrwg_tdrNs
{
  void * hrw;
};

struct spn_extent_phrwg_tdrNs_snap
{
  struct spn_extent_ring_snap * snap;
  VkBuffer                      drN;
  spn_subbuf_id_t               id;
};

void
spn_extent_phrwg_tdrNs_alloc(struct spn_extent_phrwg_tdrNs * const extent,
                             struct spn_device * const             device,
                             size_t const                          size);

void
spn_extent_phrwg_tdrNs_free(struct spn_extent_phrwg_tdrNs * const extent,
                            struct spn_device * const             device);

void
spn_extent_phrwg_tdrNs_snap_init(struct spn_extent_phrwg_tdrNs_snap * const snap,
                                 struct spn_extent_ring * const             ring,
                                 struct spn_device * const                  device);

void
spn_extent_phrwg_tdrNs_snap_alloc(struct spn_extent_phrwg_tdrNs_snap * const snap,
                                  struct spn_extent_phrwg_tdrNs * const      extent,
                                  struct spn_device * const                  device,
                                  VkCommandBuffer                            cb);

void
spn_extent_phrwg_tdrNs_snap_free(struct spn_extent_phrwg_tdrNs_snap * const snap,
                                 struct spn_device * const                  device);

//
// DURABLE HOST R/W RING WITH AN EPHEMERAL HOST R/1 SNAPSHOT
//
// Note that because the ring and snapshot are both in host memory and
// the snapshot blocks progress until freed we can simply point the
// fake ephemeral snapshot at the ring's durable extent.
//

struct spn_extent_phrwg_thr1s
{
  void * hrw;
};

struct spn_extent_phrwg_thr1s_snap
{
  struct spn_extent_ring_snap * snap;

  struct
  {
    uint32_t lo;
    uint32_t hi;
  } count;

  struct
  {
    void * lo;
    void * hi;
  } hr1;
};

void
spn_extent_phrwg_thr1s_alloc(struct spn_extent_phrwg_thr1s * const extent,
                             struct spn_device * const             device,
                             size_t const                          size);

void
spn_extent_phrwg_thr1s_free(struct spn_extent_phrwg_thr1s * const extent,
                            struct spn_device * const             device);

void
spn_extent_phrwg_thr1s_snap_init(struct spn_extent_phrwg_thr1s_snap * const snap,
                                 struct spn_extent_ring * const             ring,
                                 struct spn_device * const                  device);
void
spn_extent_phrwg_thr1s_snap_alloc(struct spn_extent_phrwg_thr1s_snap * const snap,
                                  struct spn_extent_phrwg_thr1s * const      extent,
                                  struct spn_device * const                  device);

void
spn_extent_phrwg_thr1s_snap_free(struct spn_extent_phrwg_thr1s_snap * const snap,
                                 struct spn_device * const                  device);

//
// EPHEMERAL MAPPING
//
// ENTIRE EXTENT   MAPPED TO R/W   HOST MEMORY
// ENTIRE EXTENT UNMAPPED TO R/W DEVICE MEMORY
//
// Note: integrated vs. discrete GPUs will have different
// implementations because we don't want a GPU kernel repeatedly
// accessing pinned memory.
//

struct spn_extent_thrw_tdrw
{
  VkDeviceSize    size;
  VkBuffer        drw;
  spn_subbuf_id_t id;
};

void
spn_extent_thrw_tdrw_alloc(struct spn_extent_thrw_tdrw * const extent,
                           struct spn_device * const           device,
                           size_t const                        size);

void
spn_extent_thrw_tdrw_free(struct spn_extent_thrw_tdrw * const extent,
                          struct spn_device * const           device);

void *
spn_extent_thrw_tdrw_map_size(struct spn_extent_thrw_tdrw * const extent,
                              size_t const                        size,  // FIXME {OFFSET,SIZE}
                              VkCommandBuffer                     cb);

void *
spn_extent_thrw_tdrw_map(struct spn_extent_thrw_tdrw * const extent);

void
spn_extent_thrw_tdrw_unmap(struct spn_extent_thrw_tdrw * const extent, void * const hrN);

//
// DURABLE MAPPING
//
// ENTIRE EXTENT   MAPPED TO R/W   HOST MEMORY
// ENTIRE EXTENT UNMAPPED TO R/W DEVICE MEMORY
//
// Note: integrated vs. discrete GPUs will have different
// implementations because we don't want a GPU kernel repeatedly
// accessing pinned memory.
//

struct spn_extent_phrw_pdrw
{
  VkDeviceSize size;
  VkBuffer     drw;
};

void
spn_extent_phrw_pdrw_alloc(struct spn_extent_phrw_pdrw * const extent,
                           struct spn_device * const           device,
                           size_t const                        size);

void
spn_extent_phrw_pdrw_free(struct spn_extent_phrw_pdrw * const extent,
                          struct spn_device * const           device);

void *
spn_extent_phrw_pdrw_map_size(struct spn_extent_phrw_pdrw * const extent,
                              size_t const                        size);  // FIXME {OFFSET,SIZE}

void *
spn_extent_phrw_pdrw_map(struct spn_extent_phrw_pdrw * const extent);

void
spn_extent_phrw_pdrw_unmap(struct spn_extent_phrw_pdrw * const extent, void * const hrN);

//
// DURABLE MAPPING
//
// ENTIRE EXTENT   MAPPED TO R/O   HOST MEMORY
// ENTIRE EXTENT UNMAPPED TO W/O DEVICE MEMORY
//
// Note: integrated vs. discrete GPUs will have different
// implementations because we don't want a GPU kernel repeatedly
// accessing pinned memory.
//

struct spn_extent_phrN_pdwN
{
  VkDeviceSize size;
  VkBuffer     dwN;
};

void
spn_extent_phrN_pdwN_alloc(struct spn_extent_phrN_pdwN * const extent,
                           struct spn_device * const           device,
                           size_t const                        size);

void
spn_extent_phrN_pdwN_free(struct spn_extent_phrN_pdwN * const extent,
                          struct spn_device * const           device);

void *
spn_extent_phrN_pdwN_map_size(struct spn_extent_phrN_pdwN * const extent, size_t const size);

void *
spn_extent_phrN_pdwN_map(struct spn_extent_phrN_pdwN * const extent);

void
spn_extent_phrN_pdwN_unmap(struct spn_extent_phrN_pdwN * const extent, void * const hrN);

//
// DURABLE MAPPING
//
// ENTIRE EXTENT   MAPPED TO W/O   HOST MEMORY
// ENTIRE EXTENT UNMAPPED TO R/O DEVICE MEMORY
//
// Note: integrated vs. discrete GPUs will have different
// implementations because we don't want a GPU kernel repeatedly
// accessing pinned memory.
//

struct spn_extent_phwN_pdrN
{
  VkDeviceSize size;
  VkBuffer     drN;
};

void
spn_extent_phwN_pdrN_alloc(struct spn_extent_phwN_pdrN * const extent,
                           struct spn_device * const           device,
                           size_t const                        size);

void
spn_extent_phwN_pdrN_free(struct spn_extent_phwN_pdrN * const extent,
                          struct spn_device * const           device);

void *
spn_extent_phwN_pdrN_map_size(struct spn_extent_phwN_pdrN * const extent,
                              size_t const                        size);  // FIXME {OFFSET,SIZE}

void *
spn_extent_phwN_pdrN_map(struct spn_extent_phwN_pdrN * const extent);

void
spn_extent_phwN_pdrN_unmap(struct spn_extent_phwN_pdrN * const extent, void * const hwm);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_EXTENT_H_
