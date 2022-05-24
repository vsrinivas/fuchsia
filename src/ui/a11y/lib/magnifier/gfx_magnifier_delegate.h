// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_MAGNIFIER_GFX_MAGNIFIER_DELEGATE_H_
#define SRC_UI_A11Y_LIB_MAGNIFIER_GFX_MAGNIFIER_DELEGATE_H_

#include <fuchsia/accessibility/cpp/fidl.h>

#include "src/lib/callback/scoped_task_runner.h"
#include "src/ui/a11y/lib/magnifier/magnifier_2.h"

namespace a11y {

// Owns fuchsia.accessibility.MagnificationHandler channel with scene owner, and controls
// the clip space transform on behalf of the Magnifier.
class GfxMagnifierDelegate : public Magnifier2::Delegate, public fuchsia::accessibility::Magnifier {
 public:
  GfxMagnifierDelegate() = default;
  ~GfxMagnifierDelegate() override = default;

  // |Magnifier2::Delegate|
  void SetMagnificationTransform(float scale, float x, float y,
                                 SetMagnificationTransformCallback callback) override;

  // |fuchsia::accessibility::Magnifier|
  void RegisterHandler(
      fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) override;

 private:
  fuchsia::accessibility::MagnificationHandlerPtr handler_;

  // Used to prevent SetClipSpaceTransform callbacks from referencing
  // members of this class if an instance goes out of scope.
  callback::ScopedTaskRunner handler_scope_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_MAGNIFIER_GFX_MAGNIFIER_DELEGATE_H_
