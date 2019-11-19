// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_RESOURCE_MAP_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_RESOURCE_MAP_H_

#include <unordered_map>

#include "src/ui/scenic/lib/gfx/resources/resource.h"
#include "src/ui/scenic/lib/gfx/resources/variable.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"

namespace scenic_impl {
namespace gfx {

class ResourceMap {
 public:
  explicit ResourceMap(std::shared_ptr<ErrorReporter> error_reporter = ErrorReporter::Default());
  ~ResourceMap();

  void Clear();

  // Attempt to add the resource; return true if successful.  Return false if
  // the ID is already present in the map, which is left unchanged.
  bool AddResource(ResourceId id, ResourcePtr resource);

  // Attempt to remove the specified resource.  Return true if successful, and
  // false if the ID was not present in the map.
  bool RemoveResource(ResourceId id);

  const std::unordered_map<ResourceId, ResourcePtr>& map() const { return resources_; }
  size_t size() const { return resources_.size(); }

  enum class ErrorBehavior { kDontReportErrors, kReportErrors };

  // Attempt to find the resource within the map.  If it is found, verify that
  // it has the correct type, and return it.  Return nullptr and report an
  // error if it is not found, or if type validation fails.
  //
  // example:
  // ResourceType someResource = map.FindResource<ResourceType>();
  template <class ResourceT>
  fxl::RefPtr<ResourceT> FindResource(ResourceId id,
                                      ErrorBehavior report_errors = ErrorBehavior::kReportErrors) {
    auto it = resources_.find(id);

    if (it == resources_.end()) {
      if (report_errors == ErrorBehavior::kReportErrors) {
        error_reporter_->ERROR() << "No resource exists with ID " << id;
      }
      return fxl::RefPtr<ResourceT>();
    };

    if (!it->second->IsKindOf<ResourceT>()) {
      if (report_errors == ErrorBehavior::kReportErrors) {
        error_reporter_->ERROR() << "Type mismatch for resource ID " << id << ": actual type is "
                                 << it->second->type_info().name << ", expected a sub-type of "
                                 << ResourceT::kTypeInfo.name;
      }
      return fxl::RefPtr<ResourceT>();
    }
    return it->second->As<ResourceT>();
  }

 private:
  std::unordered_map<ResourceId, ResourcePtr> resources_;
  const std::shared_ptr<ErrorReporter> error_reporter_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_RESOURCE_MAP_H_
