// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_ELEMENT_MANAGER_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_ELEMENT_MANAGER_IMPL_H_

#include <fuchsia/element/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>

#include <memory>
#include <unordered_map>

#include "src/lib/fxl/macros.h"
#include "src/modular/bin/sessionmgr/element_controller_impl.h"
#include "src/modular/bin/sessionmgr/storage/session_storage.h"

namespace modular {

class ElementManagerImpl : public fuchsia::element::Manager {
 public:
  explicit ElementManagerImpl(SessionStorage* session_storage);
  ~ElementManagerImpl() override = default;

  void Connect(fidl::InterfaceRequest<fuchsia::element::Manager> request);

  // |Manager|
  void ProposeElement(fuchsia::element::Spec spec,
                      fidl::InterfaceRequest<fuchsia::element::Controller> element_controller,
                      ProposeElementCallback callback) override;

 private:
  // Called when the story |story_id| is deleted.
  //
  // |story_id| is not guaranteed to be an element story created by |ElementManagerImpl|.
  void OnStoryStorageDeleted(std::string story_id);

  // Called when the story |story_id| is updated.
  //
  // |story_id| is not guaranteed to be an element story created by |ElementManagerImpl|.
  void OnStoryStorageUpdated(std::string story_id,
                             const fuchsia::modular::internal::StoryData& story_data);

  static fuchsia::modular::ModuleData CreateElementModuleData(fuchsia::element::Spec spec);

  SessionStorage* const session_storage_;  // Not owned.

  fidl::BindingSet<fuchsia::element::Manager> bindings_;

  // Map of story ID for each element to its |ElementControllerImpl|.
  std::unordered_map<std::string, std::unique_ptr<ElementControllerImpl>> element_controllers_;

  fxl::WeakPtrFactory<ElementManagerImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ElementManagerImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_ELEMENT_MANAGER_IMPL_H_
