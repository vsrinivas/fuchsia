// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace a11y {

ScreenReaderContext::ScreenReaderContext(std::unique_ptr<A11yFocusManager> a11y_focus_manager,
                                         TtsManager* tts_manager, SemanticsSource* semantics_source,
                                         std::string locale_id)
    : executor_(async_get_default_dispatcher()),
      a11y_focus_manager_(std::move(a11y_focus_manager)),
      tts_manager_(tts_manager),
      semantics_source_(semantics_source),
      locale_id_(std::move(locale_id)) {
  FX_DCHECK(tts_manager);
  FX_DCHECK(semantics_source_);
  tts_manager_->OpenEngine(tts_engine_ptr_.NewRequest(),
                           [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                             if (result.is_err()) {
                               FX_LOGS(ERROR) << "Unable to connect to TTS service";
                             }
                           });
  auto result = intl::Lookup::New({locale_id_});
  FX_DCHECK(result.is_ok()) << "Load of l10n resources failed.";
  auto message_formatter =
      std::make_unique<i18n::MessageFormatter>(icu::Locale("en-US"), result.take_value());
  auto screen_reader_message_generator =
      std::make_unique<ScreenReaderMessageGenerator>(std::move(message_formatter));
  speaker_ = std::make_unique<Speaker>(&executor_, &tts_engine_ptr_,
                                       std::move(screen_reader_message_generator));
}

ScreenReaderContext::ScreenReaderContext() : executor_(async_get_default_dispatcher()) {}

ScreenReaderContext::~ScreenReaderContext() {}

A11yFocusManager* ScreenReaderContext::GetA11yFocusManager() {
  FX_DCHECK(a11y_focus_manager_.get());
  return a11y_focus_manager_.get();
}

Speaker* ScreenReaderContext::speaker() {
  FX_DCHECK(speaker_.get());
  return speaker_.get();
}

bool ScreenReaderContext::IsTextFieldFocused() const {
  const auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  if (!a11y_focus) {
    return false;
  }
  const auto* node =
      semantics_source_->GetSemanticNode(a11y_focus->view_ref_koid, a11y_focus->node_id);
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
  const auto* node =
      semantics_source_->GetSemanticNode(a11y_focus->view_ref_koid, a11y_focus->node_id);
  if (!node) {
    return false;
  }

  if (node->has_attributes() && node->attributes().has_is_keyboard_key() &&
      node->attributes().is_keyboard_key()) {
    return true;
  }

  return false;
}

}  // namespace a11y
