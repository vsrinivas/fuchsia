// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/change_semantic_level_action.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {
namespace {

// Cycles through the available semantic levels, starting at |level|, where the direction is defined
// by |direction|. If |is_slider_focused| is true, the Semantic Level kAdjustValue is part of the
// list.
ScreenReaderContext::SemanticLevel NextSemanticLevelInDirection(
    ScreenReaderContext::SemanticLevel level, bool is_slider_focused,
    ChangeSemanticLevelAction::Direction direction) {
  // In the first semantic level list, all semantic levels are present. In the second, all but
  // kAdjustValue, which is not present if the focused semantic node is a slider.
  static const std::vector<ScreenReaderContext::SemanticLevel> semantic_level_list = {
      ScreenReaderContext::SemanticLevel::kNormalNavigation,
      ScreenReaderContext::SemanticLevel::kAdjustValue,
      ScreenReaderContext::SemanticLevel::kCharacter, ScreenReaderContext::SemanticLevel::kWord};
  static const std::vector<ScreenReaderContext::SemanticLevel> semantic_level_list_no_sliders = {
      ScreenReaderContext::SemanticLevel::kNormalNavigation,
      ScreenReaderContext::SemanticLevel::kCharacter, ScreenReaderContext::SemanticLevel::kWord};

  decltype(semantic_level_list)* semantic_level_list_for_node;
  if (is_slider_focused) {
    semantic_level_list_for_node = &semantic_level_list;
  } else {
    semantic_level_list_for_node = &semantic_level_list_no_sliders;
  }

  auto index = 0;
  for (const auto& item : *semantic_level_list_for_node) {
    if (level == item) {
      break;
    }
    ++index;
  }
  FX_DCHECK(index < static_cast<int>(semantic_level_list_for_node->size()));

  int n = semantic_level_list_for_node->size();
  switch (direction) {
    case ChangeSemanticLevelAction::Direction::kForward:
      index = (index + 1) % n;
      break;
    case ChangeSemanticLevelAction::Direction::kBackward:
      index = (n + (index - 1)) % n;
      break;
  }
  auto new_level = semantic_level_list_for_node->at(index);
  return new_level;
}

}  // namespace

ChangeSemanticLevelAction::ChangeSemanticLevelAction(Direction direction,
                                                     ActionContext* action_context,
                                                     ScreenReaderContext* screen_reader_context)
    : ScreenReaderAction(action_context, screen_reader_context), direction_(direction) {}

ChangeSemanticLevelAction::~ChangeSemanticLevelAction() = default;

void ChangeSemanticLevelAction::Run(ActionData process_data) {
  bool is_slider_focused = false;
  auto a11y_focus = screen_reader_context_->GetA11yFocusManager()->GetA11yFocus();
  if (a11y_focus) {
    FX_DCHECK(action_context_->semantics_source);
    auto* node = action_context_->semantics_source->GetSemanticNode(a11y_focus->view_ref_koid,
                                                                    a11y_focus->node_id);
    if (node && node->has_node_id() && node->has_role() &&
        node->role() == fuchsia::accessibility::semantics::Role::SLIDER) {
      is_slider_focused = true;
    }
  }
  auto level = screen_reader_context_->semantic_level();
  auto new_level = NextSemanticLevelInDirection(level, is_slider_focused, direction_);
  screen_reader_context_->set_semantic_level(new_level);
  // Speaks the new level to the user.
  auto promise = SpeakSemanticLevelPromise(new_level);
  screen_reader_context_->executor()->schedule_task(std::move(promise));
}

fit::promise<> ChangeSemanticLevelAction::SpeakSemanticLevelPromise(
    ScreenReaderContext::SemanticLevel semantic_level) {
  fuchsia::intl::l10n::MessageIds message_id;
  switch (semantic_level) {
    case ScreenReaderContext::SemanticLevel::kNormalNavigation:
      message_id = fuchsia::intl::l10n::MessageIds::NORMAL_NAVIGATION_GRANULARITY;
      break;
    case ScreenReaderContext::SemanticLevel::kAdjustValue:
      message_id = fuchsia::intl::l10n::MessageIds::ADJUST_VALUE_GRANULARITY;
      break;
    case ScreenReaderContext::SemanticLevel::kCharacter:
      message_id = fuchsia::intl::l10n::MessageIds::CHARACTER_GRANULARITY;
      break;
    case ScreenReaderContext::SemanticLevel::kWord:
      message_id = fuchsia::intl::l10n::MessageIds::WORD_GRANULARITY;
      break;
    default:
      return fit::make_error_promise();
  }
  auto* speaker = screen_reader_context_->speaker();
  return speaker->SpeakMessageByIdPromise(message_id, {.interrupt = true, .save_utterance = false});
}

}  // namespace a11y
