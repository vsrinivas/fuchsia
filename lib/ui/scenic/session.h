// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_SESSION_H_
#define GARNET_LIB_UI_SCENIC_SESSION_H_

#include <array>
#include <memory>

#include <fuchsia/cpp/gfx.h>
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "garnet/lib/ui/scenic/system.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace scenic {

class CommandDispatcher;
class Scenic;

using SessionId = uint64_t;

class Session final : public ui::Session,
                      public EventReporter,
                      public ErrorReporter {
 public:
  Session(Scenic* owner,
          SessionId id,
          ::fidl::InterfaceHandle<ui::SessionListener> listener);
  ~Session() override;

  void SetCommandDispatchers(
      std::array<std::unique_ptr<CommandDispatcher>,
                 System::TypeId::kMaxSystems> dispatchers);

  bool ApplyCommand(const ui::Command& command);

  // |ui::Session|
  void Enqueue(::fidl::VectorPtr<ui::Command> cmds) override;

  // |ui::Session|
  void Present(uint64_t presentation_time,
               ::fidl::VectorPtr<zx::event> acquire_fences,
               ::fidl::VectorPtr<zx::event> release_fences,
               PresentCallback callback) override;

  // |ui::Session|
  // TODO(MZ-422): Remove this after it's removed from session.fidl.
  void HitTest(uint32_t node_id,
               ::gfx::vec3 ray_origin,
               ::gfx::vec3 ray_direction,
               HitTestCallback callback) override;

  // |ui::Session|
  // TODO(MZ-422): Remove this after it's removed from session.fidl.
  void HitTestDeviceRay(::gfx::vec3 ray_origin,
                        ::gfx::vec3 ray_direction,
                        HitTestCallback callback) override;

  // |EventReporter|
  void SendEvents(::fidl::VectorPtr<ui::Event> events) override;

  // |ErrorReporter|
  // Customize behavior of ErrorReporter::ReportError().
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override;

  SessionId id() const { return id_; }

  ErrorReporter* error_reporter() { return this; }

 private:
  Scenic* const scenic_;
  const SessionId id_;
  ::fidl::InterfacePtr<ui::SessionListener> listener_;

  std::array<std::unique_ptr<CommandDispatcher>, System::TypeId::kMaxSystems>
      dispatchers_;

  fxl::WeakPtrFactory<Session> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Session);
};

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_SESSION_H_
