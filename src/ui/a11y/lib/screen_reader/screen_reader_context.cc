// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace a11y {

ScreenReaderContext::ScreenReaderContext(std::unique_ptr<A11yFocusManager> a11y_focus_manager,
                                         TtsManager* tts_manager, std::string locale_id)
    : executor_(async_get_default_dispatcher()),
      a11y_focus_manager_(std::move(a11y_focus_manager)),
      locale_id_(std::move(locale_id)) {
  FX_DCHECK(tts_manager);
  tts_manager->OpenEngine(tts_engine_ptr_.NewRequest(),
                          [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                            if (result.is_err()) {
                              FX_LOGS(ERROR) << "Unable to connect to TTS service";
                            }
                          });
  auto result = intl::Lookup::New({locale_id_});
  FX_DCHECK(result.is_ok()) << "Load of l10n resources failed.";
  auto message_formatter =
      std::make_unique<i18n::MessageFormatter>(icu::Locale("en-US"), result.take_value());
  auto node_describer = std::make_unique<NodeDescriber>(std::move(message_formatter));
  speaker_ = std::make_unique<Speaker>(&tts_engine_ptr_, std::move(node_describer));
}

ScreenReaderContext::ScreenReaderContext() : executor_(async_get_default_dispatcher()) {}

A11yFocusManager* ScreenReaderContext::GetA11yFocusManager() {
  FX_DCHECK(a11y_focus_manager_.get());
  return a11y_focus_manager_.get();
}

Speaker* ScreenReaderContext::speaker() {
  FX_DCHECK(speaker_.get());
  return speaker_.get();
}

}  // namespace a11y
