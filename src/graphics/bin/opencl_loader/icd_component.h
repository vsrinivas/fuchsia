// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_BIN_OPENCL_LOADER_ICD_COMPONENT_H_
#define SRC_GRAPHICS_BIN_OPENCL_LOADER_ICD_COMPONENT_H_

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/result.h>
#include <zircon/types.h>

#include "rapidjson/document.h"
#include "sdk/lib/sys/inspect/cpp/component.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"

class LoaderApp;

// This class represents a single CFv2 component that provides a Opencl ICD. See
// the accompanying README.md for a description of what the component must
// provide.
class IcdComponent : public std::enable_shared_from_this<IcdComponent> {
 public:
  enum class LookupStages { kStarted, kFailed, kFinished };

  static std::shared_ptr<IcdComponent> Create(sys::ComponentContext* context, LoaderApp* app,
                                              inspect::Node* parent_node,
                                              std::string component_url) {
    auto component = std::make_shared<IcdComponent>(app, std::move(component_url));
    component->Initialize(context, parent_node);
    return component;
  }

  explicit IcdComponent(LoaderApp* app, std::string component_url)
      : app_(app), component_url_(std::move(component_url)) {}

  ~IcdComponent();

  void Initialize(sys::ComponentContext* context, inspect::Node* parent_node);

  // Attempts to read and store manifest.json. Returns the library path if available.
  std::optional<std::string> ReadManifest(int contents_dir_fd, const std::string& manifest_path);

  // Validate that the metadata json matches the schema. |component_url| is used
  // only when logging errors.
  static bool ValidateMetadataJson(const std::string& component_url,
                                   const rapidjson::GenericDocument<rapidjson::UTF8<>>& doc);

  // Validate that the manifest json matches the schema. |component_url| is used
  // only when logging errors.
  static bool ValidateManifestJson(const std::string& component_url,
                                   const rapidjson::GenericDocument<rapidjson::UTF8<>>& doc);

  zx::result<zx::vmo> CloneVmo() const;

  // library_path is essentially an arbitrary string given by `library_path` from the ICD.
  std::string library_path() const {
    std::lock_guard lock(vmo_lock_);
    if (vmo_info_)
      return vmo_info_->library_path;
    return "";
  }

  LookupStages stage() const {
    std::lock_guard lock(vmo_lock_);
    return stage_;
  }

  std::optional<std::string> GetManifestFileName() {
    std::lock_guard lock(vmo_lock_);
    if (!vmo_info_)
      return {};
    return vmo_info_->library_path + ".json";
  }

  void AddManifestToFs();
  void RemoveManifestFromFs();
  const std::string& child_instance_name() const { return child_instance_name_; }

  fbl::RefPtr<fs::PseudoFile> manifest_file() { return manifest_file_; }

 private:
  struct VmoInfo {
    std::string library_path;

    zx::vmo vmo;
  };

  void ReadFromComponent(fit::deferred_callback failure_callback,
                         fidl::InterfaceHandle<fuchsia::io::Directory> out_dir);

  LoaderApp* const app_;

  const std::string component_url_;
  inspect::Node node_;
  inspect::ValueList value_list_;
  fuchsia::component::RealmPtr realm_;
  std::string child_instance_name_;
  inspect::StringProperty initialization_status_;

  fbl::RefPtr<fs::PseudoFile> manifest_file_;

  mutable std::mutex vmo_lock_;
  LookupStages stage_ FXL_GUARDED_BY(vmo_lock_) = LookupStages::kStarted;
  std::optional<VmoInfo> vmo_info_ FXL_GUARDED_BY(vmo_lock_);
};

#endif  // SRC_GRAPHICS_BIN_OPENCL_LOADER_ICD_COMPONENT_H_
