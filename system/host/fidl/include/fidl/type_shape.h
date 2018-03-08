// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_

#include <stddef.h>

class TypeShape {
public:
    constexpr TypeShape(uint64_t size, uint64_t alignment)
        : size_(size), alignment_(alignment) {}
    constexpr TypeShape()
        : TypeShape(0u, 0u) {}

    TypeShape(const TypeShape&) = default;
    TypeShape& operator=(const TypeShape&) = default;

    uint64_t Size() const { return size_; }
    uint64_t Alignment() const { return alignment_; }

private:
    uint64_t size_;
    uint64_t alignment_;
};

class FieldShape {
public:
    explicit FieldShape(TypeShape typeshape, uint64_t offset = 0u)
        : typeshape_(typeshape),
          offset_(offset) {}
    FieldShape()
        : FieldShape(TypeShape()) {}

    TypeShape& Typeshape() { return typeshape_; }
    TypeShape Typeshape() const { return typeshape_; }

    uint64_t Size() const { return typeshape_.Size(); }
    uint64_t Alignment() const { return typeshape_.Alignment(); }
    uint64_t Offset() const { return offset_; }

    void SetTypeshape(TypeShape typeshape) { typeshape_ = typeshape; }
    void SetOffset(uint64_t offset) { offset_ = offset; }

private:
    TypeShape typeshape_;
    uint64_t offset_;
};

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
