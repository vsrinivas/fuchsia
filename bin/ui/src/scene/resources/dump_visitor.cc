// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/dump_visitor.h"

#include <ostream>

#include "apps/mozart/src/scene/resources/gpu_memory.h"
#include "apps/mozart/src/scene/resources/host_memory.h"
#include "apps/mozart/src/scene/resources/image.h"
#include "apps/mozart/src/scene/resources/material.h"
#include "apps/mozart/src/scene/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene/resources/nodes/scene.h"
#include "apps/mozart/src/scene/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene/resources/nodes/tag_node.h"
#include "apps/mozart/src/scene/resources/proxy_resource.h"
#include "apps/mozart/src/scene/resources/shapes/circle_shape.h"
#include "apps/mozart/src/scene/resources/shapes/rectangle_shape.h"
#include "apps/mozart/src/scene/resources/shapes/rounded_rectangle_shape.h"
#include "lib/ftl/logging.h"

namespace mozart {
namespace scene {

DumpVisitor::DumpVisitor(std::ostream& output) : output_(output) {}

DumpVisitor::~DumpVisitor() = default;

void DumpVisitor::Visit(GpuMemory* r) {
  // To prevent address space layout leakage, we don't print the pointers.
  BeginItem("GpuMemory");
  WriteProperty("size") << r->escher_gpu_mem()->size();
  WriteProperty("offset") << r->escher_gpu_mem()->offset();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(HostMemory* r) {
  // To prevent address space layout leakage, we don't print the pointers.
  BeginItem("HostMemory");
  WriteProperty("size") << r->size();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Image* r) {
  BeginItem("Image");
  WriteProperty("width") << r->escher_image()->width();
  WriteProperty("height") << r->escher_image()->height();
  WriteProperty("format") << static_cast<int>(r->escher_image()->format());
  WriteProperty("has_depth") << r->escher_image()->has_depth();
  WriteProperty("has_stencil") << r->escher_image()->has_stencil();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(EntityNode* r) {
  BeginItem("EntityNode", r->resource_id());
  VisitNode(r);
  EndItem();
}

void DumpVisitor::Visit(ShapeNode* r) {
  BeginItem("ShapeNode", r->resource_id());
  if (r->shape()) {
    BeginSection("shape");
    r->shape()->Accept(this);
    EndSection();
  }
  if (r->material()) {
    BeginSection("material");
    r->material()->Accept(this);
    EndSection();
  }
  VisitNode(r);
  EndItem();
}

void DumpVisitor::Visit(TagNode* r) {
  BeginItem("TagNode", r->resource_id());
  WriteProperty("tag") << r->tag();
  VisitNode(r);
  EndItem();
}

void DumpVisitor::Visit(Scene* r) {
  BeginItem("Scene", r->resource_id());
  VisitNode(r);
  EndItem();
}

void DumpVisitor::VisitNode(Node* r) {
  if (!r->children().empty()) {
    BeginSection("children");
    for (auto& child : r->children()) {
      child->Accept(this);
    }
    EndSection();
  }
  if (!r->parts().empty()) {
    BeginSection("parts");
    for (auto& part : r->parts()) {
      part->Accept(this);
    }
    EndSection();
  }
  VisitResource(r);
}

void DumpVisitor::Visit(CircleShape* r) {
  BeginItem("CircleShape");
  WriteProperty("radius") << r->radius();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(RectangleShape* r) {
  BeginItem("RectangleShape");
  WriteProperty("width") << r->width();
  WriteProperty("height") << r->height();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(RoundedRectangleShape* r) {
  BeginItem("RoundedRectangleShape");
  WriteProperty("width") << r->width();
  WriteProperty("height") << r->height();
  WriteProperty("top_left_radius") << r->top_left_radius();
  WriteProperty("top_right_radius") << r->top_right_radius();
  WriteProperty("bottom_right_radius") << r->bottom_right_radius();
  WriteProperty("bottom_left_radius") << r->bottom_left_radius();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Material* r) {
  BeginItem("Material");
  WriteProperty("red") << r->red();
  WriteProperty("green") << r->green();
  WriteProperty("blue") << r->blue();
  if (r->escher_material()->texture()) {
    WriteProperty("texture.width") << r->escher_material()->texture()->width();
    WriteProperty("texture.height")
        << r->escher_material()->texture()->height();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(ProxyResource* r) {
  BeginItem("ProxyResource");
  WriteProperty("import_spec") << r->import_spec();
  WriteProperty("is_bound") << (r->bound_resource() != nullptr);
  BeginSection("delegate");
  r->ops_delegate()->Accept(this);
  EndSection();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::VisitResource(Resource* r) {
  if (!r->imports().empty()) {
    BeginSection("imports");
    for (auto& import : r->imports()) {
      import->Accept(this);
    }
    EndSection();
  }
}

void DumpVisitor::BeginItem(const char* type, int32_t key) {
  BeginLine();
  if (key >= 0)
    output_ << key << "> ";
  output_ << type;
  indentation_ += 2;
}

std::ostream& DumpVisitor::WriteProperty(const char* label) {
  property_count_++;
  if (partial_line_) {
    if (property_count_ == 1u)
      output_ << ": ";
    else
      output_ << ", ";
  } else {
    BeginLine();
  }
  output_ << label << "=";
  return output_;
}

void DumpVisitor::EndItem() {
  EndLine();
  indentation_ -= 2;
}

void DumpVisitor::BeginSection(const char* label) {
  BeginLine();
  output_ << label << "...";
  EndLine();
  indentation_ += 2;
}

void DumpVisitor::EndSection() {
  FTL_DCHECK(!partial_line_);
  indentation_ -= 2;
}

void DumpVisitor::BeginLine() {
  EndLine();
  output_ << std::string(indentation_, ' ');
  partial_line_ = true;
}

void DumpVisitor::EndLine() {
  if (!partial_line_)
    return;
  output_ << std::endl;
  partial_line_ = false;
  property_count_ = 0u;
}

}  // namespace scene
}  // namespace mozart
