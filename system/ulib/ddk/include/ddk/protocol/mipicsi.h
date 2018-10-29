// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/mipicsi.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct image_resolution image_resolution_t;
// Values for Image Formats.
typedef uint32_t image_format_t;
#define IMAGE_FORMAT_AM_RAW6 UINT32_C(1)
#define IMAGE_FORMAT_AM_RAW7 UINT32_C(2)
#define IMAGE_FORMAT_AM_RAW8 UINT32_C(3)
#define IMAGE_FORMAT_AM_RAW10 UINT32_C(4)
#define IMAGE_FORMAT_AM_RAW12 UINT32_C(5)
#define IMAGE_FORMAT_AM_RAW14 UINT32_C(6)

// Values for different MIPI modes.
typedef uint32_t mipi_modes_t;
#define MIPI_MODES_DDR_MODE UINT32_C(0)
#define MIPI_MODES_DIR_MODE UINT32_C(1)
#define MIPI_MODES_DOL_MODE UINT32_C(2)

// Values for virtual channel.
typedef uint32_t mipi_path_t;
#define MIPI_PATH_PATH0 UINT32_C(0)
#define MIPI_PATH_PATH1 UINT32_C(1)

typedef struct mipi_adap_info mipi_adap_info_t;
typedef struct mipi_info mipi_info_t;
typedef struct mipi_csi_protocol mipi_csi_protocol_t;

// Declarations

struct image_resolution {
    uint32_t width;
    uint32_t height;
};

struct mipi_adap_info {
    image_resolution_t resolution;
    image_format_t format;
    mipi_modes_t mode;
    mipi_path_t path;
};

struct mipi_info {
    uint32_t channel;
    uint32_t lanes;
    uint32_t ui_value;
    uint32_t csi_version;
};

typedef struct mipi_csi_protocol_ops {
    zx_status_t (*init)(void* ctx, const mipi_info_t* mipi_info, const mipi_adap_info_t* adap_info);
    zx_status_t (*de_init)(void* ctx);
} mipi_csi_protocol_ops_t;

struct mipi_csi_protocol {
    mipi_csi_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t mipi_csi_init(const mipi_csi_protocol_t* proto,
                                        const mipi_info_t* mipi_info,
                                        const mipi_adap_info_t* adap_info) {
    return proto->ops->init(proto->ctx, mipi_info, adap_info);
}
static inline zx_status_t mipi_csi_de_init(const mipi_csi_protocol_t* proto) {
    return proto->ops->de_init(proto->ctx);
}

__END_CDECLS;
