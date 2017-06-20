// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/scene/session.fidl.h"

namespace mozart {

const float kZeroesFloat3[3] = {0.f, 0.f, 0.f};
const float kOnesFloat3[3] = {1.f, 1.f, 1.f};
// A quaterion that has no rotation.
const float kQuaternionDefault[4] = {0.f, 0.f, 0.f, 1.f};

// Resource creation.
mozart2::OpPtr NewCreateMemoryOp(uint32_t id,
                                 mx::vmo vmo,
                                 mozart2::MemoryType memory_type);
mozart2::OpPtr NewCreateImageOp(uint32_t id,
                                uint32_t memory_id,
                                uint32_t memory_offset,
                                mozart2::ImageInfo::PixelFormat format,
                                mozart2::ImageInfo::Tiling tiling,
                                uint32_t width,
                                uint32_t height,
                                uint32_t stride);
mozart2::OpPtr NewCreateBufferOp(uint32_t id,
                                 uint32_t memory_id,
                                 uint32_t memory_offset,
                                 uint32_t num_bytes);
mozart2::OpPtr NewCreateLinkOp(uint32_t id, mx::eventpair epair);

mozart2::OpPtr NewCreateCircleOp(uint32_t id, float radius);
mozart2::OpPtr NewCreateRectangleOp(uint32_t id, float width, float height);
mozart2::OpPtr NewCreateRoundedRectangleOp(uint32_t id,
                                           float width,
                                           float height,
                                           float top_left_radius,
                                           float top_right_radius,
                                           float bottom_right_radius,
                                           float bottom_left_radius);

// Variant of NewCreateCircleOp that uses a variable radius instead of a
// constant one set at construction time.
mozart2::OpPtr NewCreateVarCircleOp(uint32_t id, uint32_t radius_var_id);
// Variant of NewCreateRectangleOp that uses a variable width/height instead of
// constant ones set at construction time.
mozart2::OpPtr NewCreateVarRectangleOp(uint32_t id,
                                       uint32_t width_var_id,
                                       uint32_t height_var_id);
// Variant of NewCreateRoundedRectangleOp that uses a variable width/height/etc.
// instead of constant ones set at construction time.
mozart2::OpPtr NewCreateVarRoundedRectangleOp(
    uint32_t id,
    uint32_t width_var_id,
    uint32_t height_var_id,
    uint32_t top_left_radius_var_id,
    uint32_t top_right_radius_var_id,
    uint32_t bottom_left_radius_var_id,
    uint32_t bottom_right_radius_var_id);

mozart2::OpPtr NewCreateMaterialOp(uint32_t id,
                                   uint32_t texture_id,
                                   uint8_t red,
                                   uint8_t green,
                                   uint8_t blue,
                                   uint8_t alpha);
mozart2::OpPtr NewCreateClipNodeOp(uint32_t id);
mozart2::OpPtr NewCreateEntityNodeOp(uint32_t id);
mozart2::OpPtr NewCreateShapeNodeOp(uint32_t id);
mozart2::OpPtr NewCreateTagNodeOp(uint32_t id, int32_t tag_value);
mozart2::OpPtr NewCreateVariableFloatOp(uint32_t id, float inital_val);

mozart2::OpPtr NewReleaseResourceOp(uint32_t id);

// Node operations.
mozart2::OpPtr NewAddChildOp(uint32_t node_id, uint32_t child_id);
mozart2::OpPtr NewAddPartOp(uint32_t node_id, uint32_t part_id);
mozart2::OpPtr NewDetachOp(uint32_t node_id);
mozart2::OpPtr NewDetachChildrenOp(uint32_t node_id);
mozart2::OpPtr NewSetTransformOp(uint32_t node_id,
                                 const float translation[3],
                                 const float scale[3],
                                 const float anchor[3],
                                 const float quaternion[4]);
mozart2::OpPtr NewSetShapeOp(uint32_t node_id, uint32_t shape_id);
mozart2::OpPtr NewSetMaterialOp(uint32_t node_id, uint32_t material_id);
mozart2::OpPtr NewSetClipOp(uint32_t node_id, uint32_t clip_id);

}  // namespace mozart
