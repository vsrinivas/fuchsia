// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"

#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <string>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {
namespace {

using fuchsia::accessibility::tts::Utterance;

}  // namespace

ScreenReaderAction::ScreenReaderAction(ActionContext* context,
                                       ScreenReaderContext* screen_reader_context)
    : action_context_(context), screen_reader_context_(screen_reader_context) {
  FX_DCHECK(action_context_);
  FX_DCHECK(screen_reader_context_);
}

ScreenReaderAction::~ScreenReaderAction() = default;

void ScreenReaderAction::ExecuteHitTesting(
    ActionContext* context, GestureContext gesture_context,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
  FX_DCHECK(context);
  FX_DCHECK(context->semantics_source);
  context->semantics_source->ExecuteHitTesting(
      gesture_context.view_ref_koid, gesture_context.CurrentCentroid(true /* local coordinates */),
      std::move(callback));
}

fpromise::promise<> ScreenReaderAction::ExecuteAccessibilityActionPromise(
    zx_koid_t view_ref_koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action) {
  fpromise::bridge<> bridge;
  action_context_->semantics_source->PerformAccessibilityAction(
      view_ref_koid, node_id, action,
      [completer = std::move(bridge.completer)](bool handled) mutable {
        if (!handled) {
          return completer.complete_error();
        }
        completer.complete_ok();
      });
  return bridge.consumer.promise_or(fpromise::error());
}

fpromise::promise<> ScreenReaderAction::SetA11yFocusPromise(zx_koid_t view_koid,
                                                            const uint32_t node_id) {
  fpromise::bridge<> bridge;
  auto* a11y_focus_manager = screen_reader_context_->GetA11yFocusManager();
  a11y_focus_manager->SetA11yFocus(
      view_koid, node_id,
      [this, completer = std::move(bridge.completer), view_koid, node_id](bool success) mutable {
        if (!success) {
          return completer.complete_error();
        }

        // Update the navigation context to reflect
        // the new focus.
        UpdateNavigationContext(view_koid, node_id);
        completer.complete_ok();
      });
  return bridge.consumer.promise_or(fpromise::error());
}

fpromise::promise<> ScreenReaderAction::BuildSpeechTaskFromNodePromise(zx_koid_t view_koid,
                                                                       uint32_t node_id,
                                                                       Speaker::Options options) {
  return fpromise::make_promise(
      [this, node_id, view_koid, options]() mutable -> fpromise::promise<> {
        const auto* node = action_context_->semantics_source->GetSemanticNode(view_koid, node_id);
        if (!node) {
          FX_LOGS(INFO) << "ScreenReaderAction: No node found for node id:" << node_id;
          return fpromise::make_error_promise();
        }

        auto* speaker = screen_reader_context_->speaker();
        FX_DCHECK(speaker);
        if (screen_reader_context_->IsVirtualKeyboardFocused()) {
          // Read the key in the virtual keyboard.
          return speaker->SpeakNodeCanonicalizedLabelPromise(node, {.interrupt = true});
        }

        // When not focusing a virtual keyboard node, just describe the node.
        return speaker->SpeakNodePromise(node, options, GetMessageContext());
      });
}

fpromise::promise<> ScreenReaderAction::BuildSpeechTaskForRangeValuePromise(zx_koid_t view_koid,
                                                                            uint32_t node_id) {
  return fpromise::make_promise([this, node_id, view_koid]() mutable -> fpromise::promise<> {
    const auto* node = action_context_->semantics_source->GetSemanticNode(view_koid, node_id);
    if (!node) {
      FX_LOGS(INFO) << "ScreenReaderAction: No node found for node id:" << node_id;
      return fpromise::make_error_promise();
    }

    std::string slider_value = GetSliderValue(*node);
    if (slider_value.empty()) {
      FX_LOGS(INFO) << "ScreenReaderAction: Slider node is missing |range_value| and |value|. "
                       "Nothing to send to TTS.";
      return fpromise::make_error_promise();
    }

    auto* speaker = screen_reader_context_->speaker();
    FX_DCHECK(speaker);

    Utterance utterance;
    utterance.set_message(slider_value);
    return speaker->SpeakMessagePromise(std::move(utterance), {.interrupt = true});
  });
}

void ScreenReaderAction::UpdateNavigationContext(zx_koid_t newly_focused_view_koid,
                                                 uint32_t newly_focused_node_id) {
  const auto& previous_navigation_context = screen_reader_context_->previous_navigation_context();
  auto view_koid = newly_focused_view_koid;
  auto node_id = newly_focused_node_id;

  // If we've entered a new view, then the previous navigation context is no
  // longer relevant, so we should clear it. Otherwise, we should set the
  // previous navigation context before we update the current.
  if (previous_navigation_context.view_ref_koid &&
      view_koid != *previous_navigation_context.view_ref_koid) {
    screen_reader_context_->set_previous_navigation_context({});
  } else {
    // Set the previous navigation context before we update the current.
    screen_reader_context_->set_previous_navigation_context(
        screen_reader_context_->current_navigation_context());
  }

  ScreenReaderContext::NavigationContext new_navigation_context;
  new_navigation_context.view_ref_koid = view_koid;

  for (const fuchsia::accessibility::semantics::Node* container :
       GetContainerNodes(view_koid, node_id, action_context_->semantics_source)) {
    ScreenReaderContext::NavigationContext::Container result;
    result.node_id = container->node_id();

    if (container->has_role() &&
        container->role() == fuchsia::accessibility::semantics::Role::TABLE &&
        container->has_attributes() && container->attributes().has_table_attributes()) {
      const auto& table_attributes = container->attributes().table_attributes();

      auto& new_table_context = result.table_context.emplace();

      if (table_attributes.has_row_header_ids()) {
        for (const auto row_header_node_id : table_attributes.row_header_ids()) {
          std::string row_header;

          const auto* row_header_node =
              action_context_->semantics_source->GetSemanticNode(view_koid, row_header_node_id);

          if (row_header_node && row_header_node->has_attributes() &&
              row_header_node->attributes().has_label()) {
            row_header = row_header_node->attributes().label();
          }

          new_table_context.row_headers.push_back(row_header);
        }
      }

      if (table_attributes.has_column_header_ids()) {
        for (const auto column_header_node_id : table_attributes.column_header_ids()) {
          std::string column_header;

          const auto* column_header_node =
              action_context_->semantics_source->GetSemanticNode(view_koid, column_header_node_id);

          if (column_header_node && column_header_node->has_attributes() &&
              column_header_node->attributes().has_label()) {
            column_header = column_header_node->attributes().label();
          }

          new_table_context.column_headers.push_back(column_header);
        }
      }

      const auto* node = action_context_->semantics_source->GetSemanticNode(view_koid, node_id);

      if (node && node->has_attributes() && node->attributes().has_table_cell_attributes()) {
        const auto& table_cell_attributes = node->attributes().table_cell_attributes();
        new_table_context.row_index = table_cell_attributes.row_index();
        new_table_context.column_index = table_cell_attributes.column_index();
      }
    }

    new_navigation_context.containers.push_back(std::move(result));
  }

  screen_reader_context_->set_current_navigation_context(new_navigation_context);
}

ScreenReaderMessageGenerator::ScreenReaderMessageContext ScreenReaderAction::GetMessageContext() {
  ScreenReaderMessageGenerator::ScreenReaderMessageContext message_context;
  const ScreenReaderContext::NavigationContext& old_navigation_context =
      screen_reader_context_->previous_navigation_context();
  const ScreenReaderContext::NavigationContext& new_navigation_context =
      screen_reader_context_->current_navigation_context();

  if (!new_navigation_context.view_ref_koid) {
    return {};
  }

  // We need to report out what has changed during this navigation:
  // - which containers, if any, were entered (i.e., they are only in new_navigation_context)
  // - which containers, if any, were exited (i.e., they are only in old_navigation_context)
  // - TODO(fxbug.dev/99248): Eventually, we will likely want to report 'whether anything changed
  //   about our context in the deepest common container' (e.g., table row/column index changes)
  auto previous_containers_iter = old_navigation_context.containers.begin();
  auto current_containers_iter = new_navigation_context.containers.begin();

  auto& entered_containers = message_context.entered_containers;
  auto& exited_containers = message_context.exited_containers;

  if (old_navigation_context.view_ref_koid &&
      old_navigation_context.view_ref_koid == new_navigation_context.view_ref_koid) {
    // Ignore all containers that are common to both navigation_contexts.
    while (previous_containers_iter < old_navigation_context.containers.end() &&
           current_containers_iter < new_navigation_context.containers.end() &&
           previous_containers_iter->node_id == current_containers_iter->node_id) {
      previous_containers_iter++;
      current_containers_iter++;
    }

    // Report any containers that were just exited.
    // Note that we won't report anything if we changed views. This is intentional.
    // (At time of writing, we clear the old navigation context when changing views anyway:
    // https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/a11y/lib/screen_reader/screen_reader_action.cc;drc=183bd4d4b67728b9220a8bb5ee68acdf0d5deb43;l=129-135)
    while (previous_containers_iter < old_navigation_context.containers.end()) {
      exited_containers.push_back(action_context_->semantics_source->GetSemanticNode(
          *old_navigation_context.view_ref_koid, previous_containers_iter->node_id));
      previous_containers_iter++;
    }
    // (Reversed to give 'deepest-first' order.)
    std::reverse(exited_containers.begin(), exited_containers.end());
  }

  // Report the containers that were just entered.
  while (current_containers_iter < new_navigation_context.containers.end()) {
    entered_containers.push_back(action_context_->semantics_source->GetSemanticNode(
        *new_navigation_context.view_ref_koid, current_containers_iter->node_id));
    current_containers_iter++;
  }

  // Report table-related changes, but only if the navigation ended directly inside a table
  // (i.e., if the deepest container in new_navigation_context.containers is a table).
  // We only report anything that changed since the last navigation.
  if (!new_navigation_context.containers.empty() &&
      new_navigation_context.containers.back().table_context) {
    ScreenReaderMessageGenerator::TableCellContext changed_table_cell_context;

    const ScreenReaderContext::TableContext& new_table_context =
        new_navigation_context.containers.back().table_context.value();
    const ScreenReaderContext::TableContext* old_table_context = nullptr;
    if (!old_navigation_context.containers.empty() &&
        old_navigation_context.containers.back().node_id ==
            new_navigation_context.containers.back().node_id) {
      old_table_context = &old_navigation_context.containers.back().table_context.value();
    }

    if (!old_table_context || new_table_context.row_index != old_table_context->row_index) {
      // Some tables may not have row headers, or they may not populate the row headers
      // field. In that case, we should not try to read the header.
      if (new_table_context.row_index < new_table_context.row_headers.size()) {
        changed_table_cell_context.row_header =
            new_table_context.row_headers[new_table_context.row_index];
      }
    }

    if (!old_table_context || new_table_context.column_index != old_table_context->column_index) {
      // Some tables may not have column headers, or they may not populate the column headers
      // field. In that case, we should not try to read the header.
      if (new_table_context.column_index < new_table_context.column_headers.size()) {
        changed_table_cell_context.column_header =
            new_table_context.column_headers[new_table_context.column_index];
      }
    }

    if (!changed_table_cell_context.row_header.empty() ||
        !changed_table_cell_context.column_header.empty()) {
      message_context.changed_table_cell_context.emplace(changed_table_cell_context);
    }
  }

  return message_context;
}

}  // namespace a11y
