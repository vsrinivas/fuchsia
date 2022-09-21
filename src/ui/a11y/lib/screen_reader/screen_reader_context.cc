// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace a11y {

ScreenReaderContext::ScreenReaderContext(std::unique_ptr<A11yFocusManager> a11y_focus_manager,
                                         TtsManager* tts_manager, ViewSource* view_source,
                                         std::string locale_id)
    : executor_(async_get_default_dispatcher()),
      a11y_focus_manager_(std::move(a11y_focus_manager)),
      tts_manager_(tts_manager),
      view_source_(view_source),
      locale_id_(std::move(locale_id)) {
  FX_CHECK(tts_manager);
  FX_CHECK(view_source_);
  tts_manager_->OpenEngine(tts_engine_ptr_.NewRequest(),
                           [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                             if (result.is_err()) {
                               FX_LOGS(ERROR) << "Unable to connect to TTS service";
                             }
                           });

  // Note that en-US is passed here so that we can fallback to use it in case the first locale is
  // not available.
  auto result = intl::Lookup::New({locale_id_});
  if (result.is_error()) {
    // Try to fallback to en-US when the locale is unknown.
    locale_id_ = "en-US";
    result = intl::Lookup::New({locale_id_});
    FX_CHECK(result.is_ok()) << "Load of l10n resources failed.";
  }
  UErrorCode icu_status = U_ZERO_ERROR;
  auto icu_locale = icu::Locale::forLanguageTag(locale_id_, icu_status);
  FX_CHECK(!U_FAILURE(icu_status)) << "Could not create an icu Locale for a11y generate messages.";

  auto message_formatter =
      std::make_unique<i18n::MessageFormatter>(std::move(icu_locale), result.take_value());
  auto screen_reader_message_generator =
      std::make_unique<ScreenReaderMessageGenerator>(std::move(message_formatter));
  speaker_ = std::make_unique<Speaker>(&executor_, &tts_engine_ptr_,
                                       std::move(screen_reader_message_generator));

  a11y_focus_manager_->set_on_a11y_focus_updated_callback(
      [this](std::optional<A11yFocusManager::A11yFocusInfo> a11y_focus) {
        if (!a11y_focus) {
          last_a11y_focused_node_ = std::nullopt;
          return;
        }
        const auto* node = GetSemanticNode(a11y_focus->view_ref_koid, a11y_focus->node_id);
        if (!node) {
          last_a11y_focused_node_ = std::nullopt;
          return;
        }

        fuchsia::accessibility::semantics::Node clone;
        node->Clone(&clone);
        last_a11y_focused_node_ = std::move(clone);
      });
}

ScreenReaderContext::ScreenReaderContext() : executor_(async_get_default_dispatcher()) {}

ScreenReaderContext::~ScreenReaderContext() {}

A11yFocusManager* ScreenReaderContext::GetA11yFocusManager() {
  FX_CHECK(a11y_focus_manager_.get());
  return a11y_focus_manager_.get();
}

Speaker* ScreenReaderContext::speaker() {
  FX_CHECK(speaker_.get());
  return speaker_.get();
}

bool ScreenReaderContext::IsTextFieldFocused() const {
  const auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  if (!a11y_focus) {
    return false;
  }

  const auto* node = GetSemanticNode(a11y_focus->view_ref_koid, a11y_focus->node_id);
  if (!node) {
    return false;
  }

  if (node->has_role() && (node->role() == fuchsia::accessibility::semantics::Role::TEXT_FIELD ||
                           node->role() == fuchsia::accessibility::semantics::Role::SEARCH_BOX)) {
    return true;
  }

  return false;
}

bool ScreenReaderContext::IsVirtualKeyboardFocused() const {
  const auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  if (!a11y_focus) {
    return false;
  }
  const auto* node = GetSemanticNode(a11y_focus->view_ref_koid, a11y_focus->node_id);
  if (!node) {
    return false;
  }

  if (node->has_attributes() && node->attributes().has_is_keyboard_key() &&
      node->attributes().is_keyboard_key()) {
    return true;
  }

  return false;
}

void ScreenReaderContext::run_and_clear_on_node_update_callback() {
  if (on_node_update_callback_) {
    on_node_update_callback_();
  }

  on_node_update_callback_ = nullptr;
}

bool ScreenReaderContext::UpdateCacheIfDescribableA11yFocusedNodeContentChanged() {
  const auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  if (!a11y_focus || !last_a11y_focused_node_) {
    return false;
  }
  const auto* node = GetSemanticNode(a11y_focus->view_ref_koid, a11y_focus->node_id);
  if (!node) {
    return false;
  }

  // Note that if both don't have attributes, they are equal. That's why the OR between the two
  // expressions here.
  const bool same_attributes =
      (!node->has_attributes() && !last_a11y_focused_node_->has_attributes()) ||
      (node->has_attributes() && last_a11y_focused_node_->has_attributes() &&
       fidl::Equals(node->attributes(), last_a11y_focused_node_->attributes()));
  const bool same_states = (!node->has_states() && !last_a11y_focused_node_->has_states()) ||
                           (node->has_states() && last_a11y_focused_node_->has_states() &&
                            fidl::Equals(node->states(), last_a11y_focused_node_->states()));
  if (!same_attributes || !same_states) {
    fuchsia::accessibility::semantics::Node clone;
    node->Clone(&clone);
    last_a11y_focused_node_ = std::move(clone);
    return true;
  }

  return false;
}

const fuchsia::accessibility::semantics::Node* ScreenReaderContext::GetSemanticNode(
    zx_koid_t view_ref_koid, uint32_t node_id) const {
  FX_DCHECK(view_source_);

  auto view_wrapper = view_source_->GetViewWrapper(view_ref_koid);
  if (!view_wrapper) {
    return nullptr;
  }

  auto* view_semantics = view_wrapper->view_semantics();
  if (!view_semantics) {
    return nullptr;
  }

  auto semantic_tree = view_semantics->GetTree();
  if (!semantic_tree) {
    return nullptr;
  }

  return semantic_tree->GetNode(node_id);
}

}  // namespace a11y
