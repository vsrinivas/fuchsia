// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sketchy/page.h"

#include <algorithm>

#include "escher/material/color_utils.h"
#include "escher/scene/model.h"
#include "escher/scene/object.h"
#include "escher/scene/stage.h"
#include "escher/shape/modifier_wobble.h"
#include "sketchy/stroke.h"

namespace sketchy {

Page::Page(escher::Escher* escher)
    : escher_(escher),
      page_material_(fxl::MakeRefCounted<escher::Material>()),
      wobble_absorber_(
          std::make_unique<escher::impl::WobbleModifierAbsorber>(escher)) {
  page_material_->set_color(vec3(0.6f, 0.6f, 0.6f));

  constexpr float h_step = 360.0 / kStrokeColorCount;
  for (size_t i = 0; i < kStrokeColorCount; ++i) {
    stroke_materials_[i] = fxl::MakeRefCounted<escher::Material>();
    stroke_materials_[i]->set_color(
        escher::HsvToLinear(escher::vec3(i * h_step, 0.7f, 0.8f)));
  }
}

Page::~Page() {}

Stroke* Page::NewStroke(StrokeId id) {
  FXL_DCHECK(strokes_.find(id) == strokes_.end());
  auto stroke = new Stroke(this, id);
  strokes_[id] = std::unique_ptr<Stroke>(stroke);
  return stroke;
}

Stroke* Page::GetStroke(StrokeId id) {
  auto it = strokes_.find(id);
  if (it == strokes_.end()) {
    return nullptr;
  }
  return it->second.get();
}

void Page::DeleteStroke(StrokeId id) {
  auto it = strokes_.find(id);
  if (it != strokes_.end()) {
    dirty_strokes_.erase(it->second.get());
    strokes_.erase(it);
  }
}

std::vector<size_t> Page::ComputeVertexCounts(const StrokePath& path) {
  std::vector<size_t> counts;
  counts.reserve(path.size());
  for (auto& seg : path) {
    constexpr float kPixelsPerDivision = 4;
    size_t divisions = static_cast<size_t>(seg.length() / kPixelsPerDivision);
    // Each "division" of the stroke consists of two vertices, and we need at
    // least 2 divisions or else Stroke::Tessellate() might barf when computing
    // the "param_incr".
    counts.push_back(std::max(divisions * 2, 4UL));
  }
  return counts;
}

// TODO: perhaps put all strokes into one shared Buffer.
void Page::FinalizeStroke(StrokeId id) {}

escher::Model* Page::GetModel(const escher::Stopwatch& stopwatch,
                              const escher::Stage* stage) {
  const float current_time_sec = stopwatch.GetElapsedSeconds();

  if (!dirty_strokes_.empty()) {
    escher::Stopwatch stopwatch;
    for (auto stroke : dirty_strokes_) {
      stroke->Tessellate();
    }
    dirty_strokes_.clear();
  }

  std::vector<escher::Object> objects;

  objects.push_back(escher::Object::NewRect(
      vec2(0.f, 0.f),
      vec2(stage->viewing_volume().width(), stage->viewing_volume().height()),
      0.f, page_material_));

  if (!strokes_.empty()) {
    const float depth_range = stage->viewing_volume().depth();
    const float depth_increment = depth_range / (strokes_.size() + 1);
    float height = depth_increment;

    size_t material_index =
        fabs(fmod(current_time_sec, kStrokeColorCount)) * 40.f;
    size_t material_step = 10;
    for (auto& pair : strokes_) {
      auto& stroke = pair.second;
      if (auto& mesh = stroke->mesh()) {
        material_index = (material_index + material_step) % kStrokeColorCount;
        objects.emplace_back(vec3(0, 0, height), mesh,
                             stroke_materials_[material_index]);

        constexpr float PI = 3.14159265359f;
        constexpr float TWO_PI = PI * 2.f;
        // TODO: the freq-mod should probably be baked into the stroke's
        // "kPerimeterPos".
        const float freq_mod = stroke->length() / 100.f;
        escher::ModifierWobble wobble_data{
            {{-1.1f * TWO_PI, 0.08f, 7.f * freq_mod},
             {-0.2f * TWO_PI, 0.1f, 23.f * freq_mod},
             {0.7f * TWO_PI, 0.3f, 5.f * freq_mod}}};
        objects.back().set_shape_modifiers(escher::ShapeModifier::kWobble);
        objects.back().set_shape_modifier_data(wobble_data);
        height += depth_increment;
      }
    }
  }

  model_ = std::make_unique<escher::Model>(std::move(objects));
  model_->set_time(current_time_sec);
  wobble_absorber_->AbsorbWobbleIfAny(model_.get());
  return model_.get();
}

void Page::Clear() {
  auto it = strokes_.begin();
  while (it != strokes_.end()) {
    if (it->second->finalized()) {
      // Remove stroke from both strokes_ and dirty_strokes_.
      dirty_strokes_.erase(it->second.get());
      it = strokes_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace sketchy
