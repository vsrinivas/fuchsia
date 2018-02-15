// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_MOZART_SESSION_H_
#define GARNET_LIB_UI_MOZART_SESSION_H_

#include <array>
#include <memory>

#include "garnet/lib/ui/mozart/event_reporter.h"
#include "garnet/lib/ui/mozart/mozart.h"
#include "garnet/lib/ui/mozart/system.h"
#include "garnet/lib/ui/mozart/util/error_reporter.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/ui/mozart/fidl/session.fidl.h"

namespace mz {

class CommandDispatcher;
class Mozart;

using SessionId = uint64_t;

class Session final : public ui_mozart::Session,
                      public EventReporter,
                      public ErrorReporter {
 public:
  Session(Mozart* owner,
          SessionId id,
          ::f1dl::InterfaceHandle<ui_mozart::SessionListener> listener);
  ~Session() override;

  void SetCommandDispatchers(
      std::array<std::unique_ptr<CommandDispatcher>,
                 System::TypeId::kMaxSystems> dispatchers);

  bool ApplyCommand(const ui_mozart::CommandPtr& command);

  // |ui_mozart::Session|
  void Enqueue(::f1dl::Array<ui_mozart::CommandPtr> cmds) override;

  // |ui_mozart::Session|
  void Present(uint64_t presentation_time,
               ::f1dl::Array<zx::event> acquire_fences,
               ::f1dl::Array<zx::event> release_fences,
               const PresentCallback& callback) override;

  // |ui_mozart::Session|
  // TODO(MZ-422): Remove this after it's removed from session.fidl.
  void HitTest(uint32_t node_id,
               scenic::vec3Ptr ray_origin,
               scenic::vec3Ptr ray_direction,
               const HitTestCallback& callback) override;

  // |ui_mozart::Session|
  // TODO(MZ-422): Remove this after it's removed from session.fidl.
  void HitTestDeviceRay(scenic::vec3Ptr ray_origin,
                        scenic::vec3Ptr ray_direction,
                        const HitTestCallback& callback) override;

  // |mz::EventReporter|
  void SendEvents(::f1dl::Array<ui_mozart::EventPtr> events) override;

  // |mz::ErrorReporter|
  // Customize behavior of mz::ErrorReporter::ReportError().
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override;

  SessionId id() const { return id_; }

 private:
  Mozart* const mozart_;
  const SessionId id_;
  ::f1dl::InterfacePtr<ui_mozart::SessionListener> listener_;

  std::array<std::unique_ptr<CommandDispatcher>, System::TypeId::kMaxSystems>
      dispatchers_;

  fxl::WeakPtrFactory<Session> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Session);
};

}  // namespace mz

#endif  // GARNET_LIB_UI_MOZART_SESSION_H_
