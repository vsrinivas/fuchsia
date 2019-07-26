// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>

#define VIRTIO_WL_F_TRANS_FLAGS (1u << 1)

__BEGIN_CDECLS

typedef struct virtio_wl_config {
  uint8_t dummy;
} __PACKED virtio_wl_config_t;

enum virtio_wl_ctrl_type {
  VIRTIO_WL_CMD_VFD_NEW = 0x100,
  VIRTIO_WL_CMD_VFD_CLOSE,
  VIRTIO_WL_CMD_VFD_SEND,
  VIRTIO_WL_CMD_VFD_RECV,
  VIRTIO_WL_CMD_VFD_NEW_CTX,
  VIRTIO_WL_CMD_VFD_NEW_PIPE,
  VIRTIO_WL_CMD_VFD_HUP,
  VIRTIO_WL_CMD_VFD_NEW_DMABUF,
  VIRTIO_WL_CMD_VFD_DMABUF_SYNC,

  VIRTIO_WL_RESP_OK = 0x1000,
  VIRTIO_WL_RESP_VFD_NEW = 0x1001,
  VIRTIO_WL_RESP_VFD_NEW_DMABUF = 0x1002,

  VIRTIO_WL_RESP_ERR = 0x1100,
  VIRTIO_WL_RESP_OUT_OF_MEMORY,
  VIRTIO_WL_RESP_INVALID_ID,
  VIRTIO_WL_RESP_INVALID_TYPE,
  VIRTIO_WL_RESP_INVALID_FLAGS,
  VIRTIO_WL_RESP_INVALID_CMD,
};

typedef struct virtio_wl_ctrl_hdr {
  uint32_t type;
  uint32_t flags;
} __PACKED virtio_wl_ctrl_hdr_t;

enum virtio_wl_vfd_flags {
  VIRTIO_WL_VFD_WRITE = 0x1,
  VIRTIO_WL_VFD_READ = 0x2,
};

typedef struct virtio_wl_ctrl_vfd {
  struct virtio_wl_ctrl_hdr hdr;
  uint32_t vfd_id;
} __PACKED virtio_wl_ctrl_vfd_t;

typedef struct virtio_wl_ctrl_vfd_new {
  struct virtio_wl_ctrl_hdr hdr;
  uint32_t vfd_id;
  uint32_t flags;
  uint64_t pfn;
  uint32_t size;
  struct {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t stride0;
    uint32_t stride1;
    uint32_t stride2;
    uint32_t offset0;
    uint32_t offset1;
    uint32_t offset2;
  } dmabuf;
} __PACKED virtio_wl_ctrl_vfd_new_t;

typedef struct virtio_wl_ctrl_vfd_send {
  struct virtio_wl_ctrl_hdr hdr;
  uint32_t vfd_id;
  uint32_t vfd_count;
} __PACKED virtio_wl_ctrl_vfd_send_t;

typedef struct virtio_wl_ctrl_vfd_recv {
  struct virtio_wl_ctrl_hdr hdr;
  uint32_t vfd_id;
  uint32_t vfd_count;
} __PACKED virtio_wl_ctrl_vfd_recv_t;

typedef struct virtio_wl_ctrl_vfd_dmabuf_sync {
  struct virtio_wl_ctrl_hdr hdr;
  uint32_t vfd_id;
  uint32_t flags;
} __PACKED virtio_wl_ctrl_vfd_dmabuf_sync_t;

__END_CDECLS
