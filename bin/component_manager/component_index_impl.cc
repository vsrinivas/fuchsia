// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/component_manager/component_index_impl.h"

#include <iostream>
#include <string>
#include <utility>

#include "lib/component/fidl/component.fidl.h"
#include "peridot/bin/component_manager/component_resources_impl.h"
#include "peridot/bin/component_manager/make_network_error.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/url/gurl.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace component {

// Standard facet names.
constexpr char kComponentFacet[] = "fuchsia:component";
constexpr char kResourcesFacet[] = "fuchsia:resources";
constexpr char kApplicationFacet[] = "fuchsia:program";

// TODO(rosswang): maybe load indexes from config file
// This path must be in sync with //packages/gn/component_manager.
constexpr char kLocalIndexPath[] = "/system/components/index.json";
// TODO(rosswang): change this to the github raw link after first push
constexpr char kCloudIndexPath[] =
    "https://storage.googleapis.com/maxwell-agents/index.json";

namespace {

void CopyJSONFieldToFidl(const rapidjson::Value& object,
                         const char* key,
                         fidl::String* string) {
  if (!object.IsObject()) {
    FXL_LOG(ERROR) << "Not a JSON object";
    return;
  }
  auto iterator = object.FindMember(key);
  if (iterator == object.MemberEnd()) {
    return;
  }
  if (!iterator->value.IsString()) {
    FXL_LOG(ERROR) << "Can't copy non-string to string";
    return;
  }
  *string = iterator->value.GetString();
}

class BarrierCallback {
 public:
  BarrierCallback(int n, std::function<void()> callback)
      : n_(n), callback_(std::move(callback)) {}

  void Decrement() {
    n_--;
    if (n_ == 0) {
      callback_();
      delete this;
    }
  }

 private:
  int n_;
  std::function<void()> callback_;
};

bool FacetInfoMatches(const rapidjson::Value& facet_data,
                      const rapidjson::Value& filter_data) {
  if (filter_data.IsNull()) {
    // This was just an existence filter, so return true.
    return true;
  }
  if (facet_data.GetType() != filter_data.GetType()) {
    return false;
  }

  if (facet_data.IsObject()) {
    // Go through each key in 'filter_data' and recursively check for the same
    // equal property in 'facet_data'. If any values in 'filter_data' don't
    // match, return false. In short ensure that 'filter_data' is a subset
    // of 'facet_data'.
    for (auto it = filter_data.MemberBegin(); it != filter_data.MemberEnd();
         ++it) {
      if (!facet_data.HasMember(it->name)) {
        return false;
      }
      if (!FacetInfoMatches(facet_data[it->name], it->value)) {
        return false;
      }
    }
    return true;
  }
  if (facet_data.IsArray()) {
    // Every array element in 'filter_data' should match an element
    // in facet_data.
    FXL_LOG(FATAL) << "Filtering by array not implemented.";
    return false;
  }
  // For primitive values we can use rapidjson's comparison operator.
  return facet_data == filter_data;

  // Unreachable.
  return true;
}

bool ManifestMatches(const ComponentManifestPtr& manifest,
                     const std::map<std::string, rapidjson::Document>& filter) {
  rapidjson::Document manifest_json_doc;
  manifest_json_doc.Parse(manifest->raw.get().c_str());

  for (const auto& it : filter) {
    // See if the manifest under consideration has this facet.
    const auto& facet_type = it.first;
    if (!manifest_json_doc.HasMember(facet_type.c_str())) {
      return false;
    }

    // See if the filter's FacetInfo matches that in the facet.
    const auto& filter_data = it.second;
    const auto& facet_data = manifest_json_doc[facet_type.c_str()];

    if (!FacetInfoMatches(facet_data, filter_data)) {
      // Nope.
      return false;
    }
  }

  return true;
}

ComponentFacetPtr MakeComponentFacet(const rapidjson::Document& doc) {
  const auto& json = doc[kComponentFacet];
  auto fidl = ComponentFacet::New();
  CopyJSONFieldToFidl(json, "url", &fidl->url);
  CopyJSONFieldToFidl(json, "name", &fidl->name);
  CopyJSONFieldToFidl(json, "version", &fidl->version);
  CopyJSONFieldToFidl(json, "other_versions", &fidl->other_versions);
  return fidl;
}

ResourcesFacetPtr MakeResourcesFacet(const rapidjson::Document& doc,
                                     const std::string& base_url) {
  url::GURL component_url(base_url);

  const auto& json = doc[kResourcesFacet];
  auto fidl = ResourcesFacet::New();
  for (auto i = json.MemberBegin(); i != json.MemberEnd(); ++i) {
    // TODO(ianloic): support more advanced resources facets.
    const std::string& relative_url = i->value.GetString();
    std::string absolute_url = component_url.Resolve(relative_url).spec();
    fidl->resource_urls.insert(i->name.GetString(), absolute_url);
  }
  return fidl;
}

ApplicationFacetPtr MakeApplicationFacet(const rapidjson::Document& doc) {
  const auto& json = doc[kApplicationFacet];
  auto fidl = ApplicationFacet::New();
  // TODO(ianloic): support arguments.
  CopyJSONFieldToFidl(json, "resource", &fidl->resource);
  CopyJSONFieldToFidl(json, "runner", &fidl->runner);
  CopyJSONFieldToFidl(json, "name", &fidl->name);
  return fidl;
}

std::pair<ComponentManifestPtr, network::NetworkErrorPtr> ParseManifest(
    const std::string& component_id,
    const std::string& contents) {
  rapidjson::Document doc;
  if (doc.Parse(contents.c_str()).HasParseError()) {
    FXL_LOG(ERROR) << "Failed to parse component manifest at: " << component_id;
    return std::make_pair(
        nullptr, MakeNetworkError(0, "Failed to parse component manifest."));
  }

  if (!doc.IsObject()) {
    FXL_LOG(ERROR) << "Component manifest " << component_id
                   << " is not a JSON object";
    return std::make_pair(
        nullptr,
        MakeNetworkError(0, "Component manifest is not a JSON object"));
  }

  if (!doc.HasMember(kComponentFacet)) {
    FXL_LOG(ERROR) << "Component " << component_id
                   << " doesn't have a component facet";
    return std::make_pair(
        nullptr,
        MakeNetworkError(0, "Component manifest missing component facet"));
  }

  auto manifest = ComponentManifest::New();
  manifest->raw = contents;
  manifest->component = MakeComponentFacet(doc);

  if (doc.HasMember(kResourcesFacet)) {
    manifest->resources = MakeResourcesFacet(doc, manifest->component->url);
  }

  if (doc.HasMember(kApplicationFacet)) {
    manifest->application = MakeApplicationFacet(doc);
  }

  return std::make_pair(std::move(manifest), nullptr);
}

}  // namespace

ComponentIndexImpl::ComponentIndexImpl(
    network::NetworkServicePtr network_service)
    : resource_loader_(
          std::make_shared<ResourceLoader>(std::move(network_service))) {
  // Initialize the local index.
  {
    std::string contents;
    FXL_CHECK(files::ReadFileToString(kLocalIndexPath, &contents));
    LoadComponentIndex(contents, kLocalIndexPath);
  }

  // Merge in cloud index.
  {
    resource_loader_->LoadResource(
        kCloudIndexPath, [this](zx::vmo vmo, network::NetworkErrorPtr error) {
          if (error) {
            FXL_LOG(WARNING) << "Failed to load cloud component index";
            return;
          }

          std::string contents;
          if (!fsl::StringFromVmo(vmo, &contents)) {
            FXL_LOG(WARNING) << "Failed to make string from cloud index vmo";
            return;
          }

          LoadComponentIndex(contents, kCloudIndexPath);
        });
  }
}

void ComponentIndexImpl::LoadComponentIndex(const std::string& contents,
                                            const std::string& path) {
  rapidjson::Document doc;
  if (doc.Parse(contents.c_str()).HasParseError()) {
    FXL_LOG(FATAL) << "Failed to parse JSON component index at: " << path;
  }

  if (!doc.IsArray()) {
    FXL_LOG(FATAL) << "Malformed component index at: " << path;
  }

  for (const rapidjson::Value& uri : doc.GetArray()) {
    local_index_.emplace_back(uri.GetString());
  }
}

void ComponentIndexImpl::GetComponent(const ::fidl::String& component_id_,
                                      const GetComponentCallback& callback_) {
  const std::string& component_id(component_id_);
  const GetComponentCallback& callback(callback_);

  FXL_VLOG(1) << "ComponentIndexImpl::GetComponent(\"" << component_id << "\")";

  resource_loader_->LoadResource(
      component_id, [this, component_id, callback](
                        zx::vmo vmo, network::NetworkErrorPtr error) {
        // Pass errors to the caller.
        if (error) {
          callback(nullptr, nullptr, std::move(error));
          return;
        }

        std::string manifest_string;
        if (!fsl::StringFromVmo(vmo, &manifest_string)) {
          FXL_LOG(ERROR) << "Failed to make string from manifest vmo";
          callback(nullptr, nullptr,
                   MakeNetworkError(500, "Failed to make string from vmo"));
          return;
        }

        ComponentManifestPtr manifest;
        std::tie(manifest, error) =
            ParseManifest(component_id, manifest_string);

        if (manifest && manifest->resources) {
          std::unique_ptr<ComponentResourcesImpl> impl =
              std::make_unique<ComponentResourcesImpl>(
                  manifest->resources->resource_urls.Clone(), resource_loader_);
          callback(std::move(manifest),
                   resources_bindings_.AddBinding(std::move(impl)), nullptr);
        } else {
          callback(std::move(manifest), nullptr, nullptr);
        }
      });
}

void ComponentIndexImpl::FindComponentManifests(
    fidl::Map<fidl::String, fidl::String> filter_fidl,
    const FindComponentManifestsCallback& callback) {
  // Convert the filter from a FIDL Map of JSON to something we work on
  // internally.
  auto* filter = new std::map<std::string, rapidjson::Document>();
  for (auto i = filter_fidl.cbegin(); i != filter_fidl.cend(); ++i) {
    rapidjson::Document filter_doc;
    filter_doc.Parse(i.GetValue().get().c_str());
    if (filter_doc.HasParseError()) {
      FXL_LOG(ERROR) << "Failed to parse JSON for facet " << i.GetKey() << " : "
                     << i.GetValue();
      delete filter;
      callback(nullptr);
      return;
    }
    filter->insert(
        std::make_pair(std::string(i.GetKey()), std::move(filter_doc)));
  }

  auto* results = new std::vector<ComponentManifestPtr>();
  // Self-deleting.
  BarrierCallback* barrier =
      new BarrierCallback(local_index_.size(), [results, filter, callback] {
        fidl::Array<ComponentManifestPtr> fidl_results;
        fidl_results.Swap(results);
        delete results;
        delete filter;
        callback(std::move(fidl_results));
      });

  for (const std::string& uri : local_index_) {
    GetComponent(fidl::String(uri),
                 [results, barrier, filter](
                     ComponentManifestPtr manifest,
                     fidl::InterfaceHandle<ComponentResources> resources_handle,
                     network::NetworkErrorPtr network_error) mutable {
                   // Check if the manifest matches.
                   if (!network_error && ManifestMatches(manifest, *filter)) {
                     results->push_back(std::move(manifest));
                   }
                   barrier->Decrement();
                 });
  }
}

}  // namespace component
