// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/dump_visitor.h"

#include <lib/syslog/cpp/macros.h>

#include <ostream>

#include "src/ui/scenic/lib/gfx/resources/buffer.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/display_compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/image.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe_base.h"
#include "src/ui/scenic/lib/gfx/resources/lights/ambient_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/directional_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/point_light.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/opacity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/circle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/mesh_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"

namespace scenic_impl {
namespace gfx {

using escher::operator<<;

DumpVisitor::DumpVisitor(VisitorContext context) : context_(std::move(context)) {}

void DumpVisitor::Visit(Memory* r) {
  // To prevent address space layout leakage, we don't print the pointers.
  BeginItem("Memory", r);
  WriteProperty("is_host") << r->is_host();
  WriteProperty("size") << r->size();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::VisitEscherImage(escher::Image* i) {
  BeginSection("escher::Image");
  if (i) {
    WriteProperty("width") << i->width();
    WriteProperty("height") << i->height();
    WriteProperty("size") << i->size();
    WriteProperty("format") << static_cast<int>(i->format());
    WriteProperty("has_depth") << i->has_depth();
    WriteProperty("has_stencil") << i->has_stencil();
    WriteProperty("use_protected_memory") << i->use_protected_memory();
  } else {
    WriteProperty("value") << "(null)";
  }
  EndSection();
}

void DumpVisitor::Visit(Image* r) {
  BeginItem("Image", r);
  VisitEscherImage(r->GetEscherImage().get());
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Buffer* r) {
  BeginItem("Buffer", r);
  WriteProperty("size") << r->size();
  BeginSection("memory");
  if (r->backing_resource()) {
    r->backing_resource()->Accept(this);
  }
  EndSection();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(ImagePipeBase* r) {
  BeginItem("ImagePipe", r);
  if (r->GetEscherImage()) {
    VisitEscherImage(r->GetEscherImage().get());
  }
  VisitResource(r);
  EndItem();
}

// NOTE: unlike the other visited types, there is no Begin/EndItem() pair in this method, because
// we don't want to add an additional layer of nesting when calling this from Visit(ViewNode*).
void DumpVisitor::Visit(View* r) {
  ViewHolder* vh = r->view_holder();
  WriteProperty("view") << r->global_id() << "->" << (vh ? vh->global_id() : GlobalId());
  WriteProperty("view_ref_koid") << r->view_ref_koid();
  VisitResource(r);

  // Debug names are considered PII, therefore not included in the textual scene dump.
  if (context_.view_debug_names && !r->debug_name().empty()) {
    (*context_.view_debug_names)[r->global_id()] = r->debug_name();
  }
}

void DumpVisitor::Visit(ViewNode* r) {
  BeginItem("ViewNode", r);
  if (auto view = r->GetView()) {
    Visit(view);
  }
  VisitNode(r);
  EndItem();
}

void DumpVisitor::Visit(ViewHolder* r) {
  BeginItem("ViewHolder", r);
  View* v = r->view();
  WriteProperty("view_holder") << r->global_id() << "->" << (v ? v->global_id() : GlobalId());
  WriteProperty("focus_change") << r->GetViewProperties().focus_change;
  VisitNode(r);
  EndItem();

  // Debug names are considered PII, therefore not included in the textual scene dump.
  if (context_.view_holder_debug_names && !r->debug_name().empty()) {
    (*context_.view_holder_debug_names)[r->global_id()] = r->debug_name();
  }
}

void DumpVisitor::Visit(EntityNode* r) {
  BeginItem("EntityNode", r);
  VisitNode(r);
  EndItem();
}

void DumpVisitor::Visit(OpacityNode* r) {
  BeginItem("OpacityNode", r);
  WriteProperty("opacity") << r->opacity();
  VisitNode(r);
  EndItem();
}

void DumpVisitor::Visit(ShapeNode* r) {
  BeginItem("ShapeNode", r);
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

void DumpVisitor::Visit(Scene* r) {
  BeginItem("Scene", r);

  const bool has_lights = !r->ambient_lights().empty() || !r->directional_lights().empty() ||
                          !r->point_lights().empty();
  if (has_lights) {
    BeginSection("lights");
    for (auto& light : r->ambient_lights()) {
      light->Accept(this);
    }
    for (auto& light : r->directional_lights()) {
      light->Accept(this);
    }
    for (auto& light : r->point_lights()) {
      light->Accept(this);
    }
    EndSection();
  }

  WriteProperty("view_ref_koid") << r->view_ref_koid();
  VisitNode(r);
  EndItem();
}

void DumpVisitor::VisitNode(Node* r) {
  if (r->hit_test_behavior() != ::fuchsia::ui::gfx::HitTestBehavior::kDefault) {
    WriteProperty("hit_test_behavior") << static_cast<int>(r->hit_test_behavior());
  }
  if (r->clip_to_self()) {
    WriteProperty("clip_to_self") << r->clip_to_self();
  }
  if (r->transform().IsIdentity()) {
    WriteProperty("transform") << "identity";
  } else {
    WriteProperty("transform") << r->transform();
  }
  if (!r->children().empty()) {
    BeginSection("children");
    for (auto& child : r->children()) {
      child->Accept(this);
    }
    EndSection();
  }
  VisitResource(r);
}

void DumpVisitor::Visit(CircleShape* r) {
  BeginItem("CircleShape", r);
  WriteProperty("radius") << r->radius();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(RectangleShape* r) {
  BeginItem("RectangleShape", r);
  WriteProperty("width") << r->width();
  WriteProperty("height") << r->height();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(RoundedRectangleShape* r) {
  BeginItem("RoundedRectangleShape", r);
  WriteProperty("width") << r->width();
  WriteProperty("height") << r->height();
  WriteProperty("top_left_radius") << r->top_left_radius();
  WriteProperty("top_right_radius") << r->top_right_radius();
  WriteProperty("bottom_right_radius") << r->bottom_right_radius();
  WriteProperty("bottom_left_radius") << r->bottom_left_radius();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(MeshShape* r) {
  BeginItem("MeshShape", r);
  if (auto& mesh = r->escher_mesh()) {
    WriteProperty("num_indices") << mesh->num_indices();
    WriteProperty("num_vertices") << mesh->num_vertices();
    WriteProperty("index_buffer_offset") << mesh->index_buffer_offset();
    WriteProperty("vertex_buffer_offset") << mesh->attribute_buffer(0).offset;
    WriteProperty("vertex_buffer_stride") << mesh->attribute_buffer(0).stride;
    BeginSection("index_buffer");
    r->index_buffer()->Accept(this);
    EndSection();
    BeginSection("vertex_buffer");
    r->vertex_buffer()->Accept(this);
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Material* r) {
  BeginItem("Material", r);
  WriteProperty("red") << r->red();
  WriteProperty("green") << r->green();
  WriteProperty("blue") << r->blue();
  WriteProperty("alpha") << r->alpha();
  BeginSection("image");
  if (auto backing_image = r->texture_image()) {
    backing_image->Accept(this);
  } else {
    WriteProperty("value") << "(null)";
  }
  EndSection();
  BeginSection("texture");
  if (auto texture = r->escher_material()->texture()) {
    WriteProperty("width") << texture->width();
    WriteProperty("height") << texture->height();
    WriteProperty("size") << texture->image()->size();
  } else {
    WriteProperty("value") << "(null)";
  }
  EndSection();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Compositor* r) {
  BeginItem("Compositor", r);
  if (r->layer_stack()) {
    BeginSection("stack");
    r->layer_stack()->Accept(this);
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(DisplayCompositor* r) {
  BeginItem("DisplayCompositor", r);
  if (r->layer_stack()) {
    BeginSection("stack");
    r->layer_stack()->Accept(this);
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(LayerStack* r) {
  BeginItem("LayerStack", r);
  if (!r->layers().empty()) {
    BeginSection("layers");
    for (auto& layer : r->layers()) {
      layer->Accept(this);
    }
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Layer* r) {
  BeginItem("Layer", r);
  WriteProperty("width") << r->width();
  WriteProperty("height") << r->height();
  if (r->renderer()) {
    BeginSection("renderer");
    r->renderer()->Accept(this);
    EndSection();
  } else {
    // TODO(fxbug.dev/23495): Texture or ImagePipe or whatever.
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Camera* r) {
  BeginItem("Camera", r);
  WriteProperty("position") << r->eye_position();
  WriteProperty("look_at") << r->eye_look_at();
  WriteProperty("up") << r->eye_up();
  BeginSection("scene");
  r->scene()->Accept(this);
  EndSection();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Renderer* r) {
  BeginItem("Renderer", r);
  if (r->camera()) {
    BeginSection("camera");
    r->camera()->Accept(this);
    EndSection();
  }
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(Light* r) { FX_CHECK(false) << "implement Visit() in Light subclasses"; }

void DumpVisitor::Visit(AmbientLight* r) {
  BeginItem("AmbientLight", r);
  WriteProperty("color") << r->color();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(DirectionalLight* r) {
  BeginItem("DirectionalLight", r);
  WriteProperty("direction") << r->direction();
  WriteProperty("color") << r->color();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::Visit(PointLight* r) {
  BeginItem("PointLight", r);
  WriteProperty("position") << r->position();
  WriteProperty("color") << r->color();
  VisitResource(r);
  EndItem();
}

void DumpVisitor::VisitResource(Resource* r) {
  if (r->event_mask()) {
    BeginSection("mask");
    WriteProperty("event_mask") << r->event_mask();
    EndSection();
  }

  if (context_.visited) {
    context_.visited->emplace(r->global_id());
  }
}

void DumpVisitor::BeginItem(const char* type, Resource* r) {
  BeginLine();
  if (r) {
    context_.output << r->global_id();
    if (!r->label().empty())
      context_.output << ":\"" << r->label() << "\"";
    context_.output << "> ";
  }
  context_.output << type;
  indentation_ += 1;
}

std::ostream& DumpVisitor::WriteProperty(const char* label) {
  property_count_++;
  if (partial_line_) {
    if (property_count_ == 1u)
      context_.output << ": ";
    else
      context_.output << ", ";
  } else {
    BeginLine();
  }
  context_.output << label << "=";
  return context_.output;
}

void DumpVisitor::EndItem() {
  EndLine();
  indentation_ -= 1;
}

void DumpVisitor::BeginSection(const char* label) {
  BeginLine();
  context_.output << label << ":";
  EndLine();
}

void DumpVisitor::EndSection() { EndLine(); }

void DumpVisitor::BeginLine() {
  EndLine();
  context_.output << std::string(indentation_, ' ');
  partial_line_ = true;
}

void DumpVisitor::EndLine() {
  if (!partial_line_)
    return;
  context_.output << std::endl;
  partial_line_ = false;
  property_count_ = 0u;
}

}  // namespace gfx
}  // namespace scenic_impl
