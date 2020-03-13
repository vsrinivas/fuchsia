// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/story_storage_xdr.h"

#include <src/modular/bin/sessionmgr/storage/annotation_xdr.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace modular {

// Serialization and deserialization of fuchsia::modular::ModuleData
// and fuchsia::modular::StoryInfo to and from JSON.

namespace {

void XdrLinkPath(XdrContext* const xdr, fuchsia::modular::LinkPath* const data) {
  xdr->Field("module_path", &data->module_path);
  xdr->Field("link_name", &data->link_name);
}

void XdrModuleParameterMapEntry(XdrContext* const xdr,
                                fuchsia::modular::ModuleParameterMapEntry* const data) {
  // NOTE: the JSON field naming doesn't match the FIDL struct naming because
  // the field names in FIDL were changed.
  xdr->Field("key", &data->name);
  xdr->Field("link_path", &data->link_path, XdrLinkPath);
}

void XdrModuleParameterMap(XdrContext* const xdr,
                           fuchsia::modular::ModuleParameterMap* const data) {
  // NOTE: the JSON field naming doesn't match the FIDL struct naming because
  // the field names in FIDL were changed.
  xdr->Field("key_to_link_map", &data->entries, XdrModuleParameterMapEntry);
}

void XdrSurfaceRelation(XdrContext* const xdr, fuchsia::modular::SurfaceRelation* const data) {
  xdr->Field("arrangement", &data->arrangement);
  xdr->Field("dependency", &data->dependency);
  xdr->Field("emphasis", &data->emphasis);
}

void XdrIntentParameterData(XdrContext* const xdr,
                            fuchsia::modular::IntentParameterData* const data) {
  static constexpr char kTag[] = "tag";
  static constexpr char kEntityReference[] = "entity_reference";
  static constexpr char kJson[] = "json";
  static constexpr char kEntityType[] = "entity_type";

  switch (xdr->op()) {
    case XdrOp::FROM_JSON: {
      std::string tag;
      xdr->Field(kTag, &tag);

      if (tag == kEntityReference) {
        fidl::StringPtr value;
        xdr->Field(kEntityReference, &value);
        data->set_entity_reference(value.value_or(""));
      } else if (tag == kJson) {
        fidl::StringPtr value;
        xdr->Field(kJson, &value);
        fsl::SizedVmo vmo;
        FX_CHECK(fsl::VmoFromString(*value, &vmo));
        data->set_json(std::move(vmo).ToTransport());
      } else if (tag == kEntityType) {
        ::std::vector<::std::string> value;
        xdr->Field(kEntityType, &value);
        data->set_entity_type(std::move(value));
      } else {
        FX_LOGS(ERROR) << "XdrIntentParameterData FROM_JSON unknown tag: " << tag;
      }
      break;
    }

    case XdrOp::TO_JSON: {
      std::string tag;

      switch (data->Which()) {
        case fuchsia::modular::IntentParameterData::Tag::kEntityReference: {
          tag = kEntityReference;
          fidl::StringPtr value = data->entity_reference();
          xdr->Field(kEntityReference, &value);
          break;
        }
        case fuchsia::modular::IntentParameterData::Tag::kJson: {
          tag = kJson;
          std::string json_string;
          FX_CHECK(fsl::StringFromVmo(data->json(), &json_string));
          fidl::StringPtr value = json_string;
          xdr->Field(kJson, &value);
          break;
        }
        case fuchsia::modular::IntentParameterData::Tag::kEntityType: {
          tag = kEntityType;
          std::vector<std::string> value = data->entity_type();
          xdr->Field(kEntityType, &value);
          break;
        }
        case fuchsia::modular::IntentParameterData::Tag::Invalid:
          FX_LOGS(ERROR) << "XdrIntentParameterData TO_JSON unknown tag: "
                         << static_cast<int>(data->Which());
          break;
      }

      xdr->Field(kTag, &tag);
      break;
    }
  }
}

void XdrIntentParameter(XdrContext* const xdr, fuchsia::modular::IntentParameter* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("data", &data->data, XdrIntentParameterData);
}

void XdrIntent(XdrContext* const xdr, fuchsia::modular::Intent* const data) {
  xdr->Field("action_name", &data->action);
  xdr->Field("action_handler", &data->handler);
  xdr->Field("parameters", &data->parameters, XdrIntentParameter);
}

void XdrModuleData_v1(XdrContext* const xdr, fuchsia::modular::ModuleData* const data) {
  xdr->Field("url", data->mutable_module_url());
  xdr->Field("module_path", data->mutable_module_path());
  xdr->Field("module_source", data->mutable_module_source());
  xdr->Field("surface_relation", data->mutable_surface_relation(), XdrSurfaceRelation);
  xdr->Field("module_stopped", data->mutable_module_deleted());
  xdr->Field("intent", data->mutable_intent(), XdrIntent);

  // In previous versions we did not have these fields.
  data->mutable_parameter_map()->entries.resize(0);
}

void XdrModuleData_v2(XdrContext* const xdr, fuchsia::modular::ModuleData* const data) {
  xdr->Field("url", data->mutable_module_url());
  xdr->Field("module_path", data->mutable_module_path());
  xdr->Field("module_source", data->mutable_module_source());
  xdr->Field("surface_relation", data->mutable_surface_relation(), XdrSurfaceRelation);
  xdr->Field("module_stopped", data->mutable_module_deleted());
  xdr->Field("intent", data->mutable_intent(), XdrIntent);
  // NOTE: the JSON field naming doesn't match the FIDL struct naming because
  // the field name in FIDL was changed.
  xdr->Field("chain_data", data->mutable_parameter_map(), XdrModuleParameterMap);
}

void XdrModuleData_v3(XdrContext* const xdr, fuchsia::modular::ModuleData* const data) {
  xdr->Field("url", data->mutable_module_url());
  xdr->Field("module_path", data->mutable_module_path());
  xdr->Field("module_source", data->mutable_module_source());
  xdr->Field("surface_relation", data->mutable_surface_relation(), XdrSurfaceRelation);
  xdr->Field("module_stopped", data->mutable_module_deleted());
  xdr->Field("intent", data->mutable_intent(), XdrIntent);
  // NOTE: the JSON field naming doesn't match the FIDL struct naming because
  // the field name in FIDL was changed.
  xdr->Field("chain_data", data->mutable_parameter_map(), XdrModuleParameterMap);
}

void XdrModuleData_v4(XdrContext* const xdr, fuchsia::modular::ModuleData* const data) {
  if (!xdr->Version(4)) {
    return;
  }
  xdr->Field("url", data->mutable_module_url());
  xdr->Field("module_path", data->mutable_module_path());
  xdr->Field("module_source", data->mutable_module_source());
  xdr->Field("surface_relation", data->mutable_surface_relation(), XdrSurfaceRelation);
  xdr->Field("module_stopped", data->mutable_module_deleted());
  xdr->Field("intent", data->mutable_intent(), XdrIntent);
  // NOTE: the JSON field naming doesn't match the FIDL struct naming because
  // the field name in FIDL was changed.
  xdr->Field("chain_data", data->mutable_parameter_map(), XdrModuleParameterMap);
}

void XdrModuleData_v5(XdrContext* const xdr, fuchsia::modular::ModuleData* const data) {
  if (!xdr->Version(5)) {
    return;
  }
  xdr->Field("url", data->mutable_module_url());
  xdr->Field("module_path", data->mutable_module_path());
  xdr->Field("module_source", data->mutable_module_source());
  xdr->Field("surface_relation", data->mutable_surface_relation(), XdrSurfaceRelation);
  xdr->Field("module_deleted", data->mutable_module_deleted());
  xdr->Field("intent", data->mutable_intent(), XdrIntent);
  // NOTE: the JSON field naming doesn't match the FIDL struct naming because
  // the field name in FIDL was changed.
  xdr->Field("chain_data", data->mutable_parameter_map(), XdrModuleParameterMap);
}

void XdrModuleData_v6(XdrContext* const xdr, fuchsia::modular::ModuleData* const data) {
  if (!xdr->Version(6)) {
    return;
  }
  xdr->Field("url", data->mutable_module_url());
  xdr->Field("module_path", data->mutable_module_path());
  xdr->Field("module_source", data->mutable_module_source());
  xdr->Field("surface_relation", data->mutable_surface_relation(), XdrSurfaceRelation);
  xdr->Field("module_deleted", data->mutable_module_deleted());
  xdr->Field("intent", data->mutable_intent(), XdrIntent);
  // NOTE: the JSON field naming doesn't match the FIDL struct naming because
  // the field name in FIDL was changed.
  xdr->Field("chain_data", data->mutable_parameter_map(), XdrModuleParameterMap);
  xdr->Field("is_embedded", data->mutable_is_embedded());
}

void XdrModuleData_v7(XdrContext* const xdr, fuchsia::modular::ModuleData* const data) {
  if (!xdr->Version(7)) {
    return;
  }
  if (xdr->HasField("url", data->has_module_url()))
    xdr->Field("url", data->mutable_module_url());
  else
    data->clear_module_url();
  if (xdr->HasField("module_path", data->has_module_path()))
    xdr->Field("module_path", data->mutable_module_path());
  else
    data->clear_module_path();
  if (xdr->HasField("module_source", data->has_module_source()))
    xdr->Field("module_source", data->mutable_module_source());
  else
    data->clear_module_source();
  if (xdr->HasField("surface_relation", data->has_surface_relation()))
    xdr->Field("surface_relation", data->mutable_surface_relation(), XdrSurfaceRelation);
  else
    data->clear_surface_relation();
  if (xdr->HasField("module_deleted", data->has_module_deleted()))
    xdr->Field("module_deleted", data->mutable_module_deleted());
  else
    data->clear_module_deleted();
  if (xdr->HasField("intent", data->has_intent()))
    xdr->Field("intent", data->mutable_intent(), XdrIntent);
  else
    data->clear_intent();
  // NOTE: the JSON field naming doesn't match the FIDL struct naming because
  // the field name in FIDL was changed.
  if (xdr->HasField("chain_data", data->has_parameter_map()))
    xdr->Field("chain_data", data->mutable_parameter_map(), XdrModuleParameterMap);
  else
    data->clear_parameter_map();
  if (xdr->HasField("is_embedded", data->has_is_embedded()))
    xdr->Field("is_embedded", data->mutable_is_embedded());
  else
    data->clear_is_embedded();
  if (xdr->HasField("annotations", data->has_annotations()))
    xdr->Field("annotations", data->mutable_annotations(), XdrAnnotation);
  else
    data->clear_annotations();
}

}  // namespace

// clang-format off
XdrFilterType<fuchsia::modular::ModuleData> XdrModuleData[] = {
    XdrModuleData_v7,
    XdrModuleData_v6,
    XdrModuleData_v5,
    XdrModuleData_v4,
    XdrModuleData_v3,
    XdrModuleData_v2,
    XdrModuleData_v1,
    nullptr,
};
// clang-format on

}  // namespace modular
