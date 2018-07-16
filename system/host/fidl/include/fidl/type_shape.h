// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_

#include <stddef.h>

class TypeShape {
public:
    constexpr TypeShape(uint32_t size, uint32_t alignment, uint32_t depth = 0u, uint32_t max_handles = 0u)
        : size_(size), alignment_(alignment), depth_(depth), max_handles_(max_handles) {}
    constexpr TypeShape()
        : TypeShape(0u, 0u, 0u, false) {}

    TypeShape(const TypeShape&) = default;
    TypeShape& operator=(const TypeShape&) = default;

    uint32_t Size() const { return size_; }
    uint32_t Alignment() const { return alignment_; }
    uint32_t Depth() const { return depth_; }
    uint32_t MaxHandles() const { return max_handles_; }

private:
    uint32_t size_;
    uint32_t alignment_;
    uint32_t depth_;
    uint32_t max_handles_;
};

class FieldShape {
public:
    explicit FieldShape(TypeShape typeshape, uint32_t offset = 0u)
        : typeshape_(typeshape), offset_(offset) {}
    FieldShape()
        : FieldShape(TypeShape()) {}

    TypeShape& Typeshape() { return typeshape_; }
    TypeShape Typeshape() const { return typeshape_; }

    uint32_t Size() const { return typeshape_.Size(); }
    uint32_t Alignment() const { return typeshape_.Alignment(); }
    uint32_t Depth() const { return typeshape_.Depth(); }
    uint32_t Offset() const { return offset_; }
    uint32_t MaxHandles() const { return typeshape_.MaxHandles(); }

    void SetTypeshape(TypeShape typeshape) { typeshape_ = typeshape; }
    void SetOffset(uint32_t offset) { offset_ = offset; }

private:
    TypeShape typeshape_;
    uint32_t offset_;
};

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
