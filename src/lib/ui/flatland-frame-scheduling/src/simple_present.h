// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_UI_FLATLAND_FRAME_SCHEDULING_SRC_SIMPLE_PRESENT_H_
#define SRC_LIB_UI_FLATLAND_FRAME_SCHEDULING_SRC_SIMPLE_PRESENT_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <queue>

namespace simple_present {
using OnFramePresentedCallback = fit::function<void(zx_time_t actual_presentation_time)>;
using OnErrorCallback = fit::function<void()>;

// This class is meant to help clients use the Flatland Present API correctly.
class FlatlandConnection final {
 public:
  ~FlatlandConnection();
  FlatlandConnection(const FlatlandConnection&) = delete;
  FlatlandConnection& operator=(const FlatlandConnection&) = delete;

  // Creates a flatland connection using the given |context|.
  static std::unique_ptr<FlatlandConnection> Create(sys::ComponentContext* context,
                                                    const std::string& debug_name);
  // Creates a flatland connection using fdio_service_connect.
  static std::unique_ptr<FlatlandConnection> Create(const std::string& debug_name);
  // Creates a flatland connection by binding the given channel.
  static std::unique_ptr<FlatlandConnection> Create(zx::channel flatland_endpoint,
                                                    const std::string& debug_name);

  fuchsia::ui::composition::Flatland* flatland() { return flatland_.get(); }

  void SetErrorCallback(OnErrorCallback callback);

  // Safe attempt to Present(). It goes through with default present args if present credits are
  // available.
  void Present();

  // This version of Present can be readily used for steady-state rendering. Inside |callback|
  // clients may process any input, submit Flatland commands, and finally re-Present(), perpetuating
  // the loop.
  void Present(fuchsia::ui::composition::PresentArgs present_args,
               OnFramePresentedCallback callback);

 private:
  FlatlandConnection(fuchsia::ui::composition::FlatlandPtr flatland, const std::string& debug_name);

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
  OnErrorCallback error_callback_;
};

}  // namespace simple_present

#endif  // SRC_LIB_UI_FLATLAND_FRAME_SCHEDULING_SRC_SIMPLE_PRESENT_H_
