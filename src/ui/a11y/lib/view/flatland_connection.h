// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_FLATLAND_CONNECTION_H_
#define SRC_UI_A11Y_LIB_VIEW_FLATLAND_CONNECTION_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <queue>

namespace a11y {

class FlatlandConnection final {
 public:
  explicit FlatlandConnection(fuchsia::ui::composition::FlatlandPtr flatland,
                              const std::string& debug_name);
  ~FlatlandConnection();

  FlatlandConnection(const FlatlandConnection&) = delete;
  FlatlandConnection& operator=(const FlatlandConnection&) = delete;

  fuchsia::ui::composition::Flatland* flatland() { return flatland_.get(); }

  using OnFramePresentedCallback = fit::function<void(zx_time_t actual_presentation_time)>;
  void Present();
  void Present(fuchsia::ui::composition::PresentArgs present_args,
               OnFramePresentedCallback callback);

 private:
  void OnError(fuchsia::ui::composition::FlatlandError error);
  void OnNextFrameBegin(fuchsia::ui::composition::OnNextFrameBeginValues values);
  void OnFramePresented(fuchsia::scenic::scheduling::FramePresentedInfo info);

  fuchsia::ui::composition::FlatlandPtr flatland_;
  uint32_t present_credits_ = 1;

  struct PendingPresent {
    PendingPresent(fuchsia::ui::composition::PresentArgs present_args,
                   OnFramePresentedCallback callback);
    ~PendingPresent();

    PendingPresent(PendingPresent&& other);
    PendingPresent& operator=(PendingPresent&& other);

    fuchsia::ui::composition::PresentArgs present_args;
    OnFramePresentedCallback callback;
  };
  std::queue<PendingPresent> pending_presents_;
  std::vector<zx::event> previous_present_release_fences_;
  std::queue<OnFramePresentedCallback> presented_callbacks_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIEW_FLATLAND_CONNECTION_H_
