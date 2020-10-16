// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/component_id_index.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <unordered_set>

#include <rapidjson/document.h>

#include "src/lib/files/file.h"

namespace component {
namespace {
const char kIndexFilePath[] = "component_id_index";

bool IsValidInstanceId(const std::string& instance_id) {
  // * 256-bits encoded in base16 = 64 characters
  //   - 1 char to represent 4 bits.
  if (instance_id.length() != 64) {
    return false;
  }
  for (size_t i = 0; i < 64; i++) {
    const auto& c = instance_id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

struct ComponentIdEntry {
  ComponentIdIndex::InstanceId id;
  std::vector<Moniker> monikers;
};

// Parses |json| to append realm path entries to |realm_path_out|.
// Returns false if |json| does not contain a valid realm path.
bool ParseRealmPath(const rapidjson::Value& json, std::vector<std::string>* realm_path_out) {
  realm_path_out->clear();
  if (!json.IsArray() || json.GetArray().Size() < 1)
    return false;

  const auto& json_array = json.GetArray();
  for (const auto& realm_name : json_array) {
    if (!realm_name.IsString())
      return false;
    realm_path_out->push_back(realm_name.GetString());
  }

  return true;
}

fit::result<ComponentIdEntry, ComponentIdIndex::Error> ParseEntry(const rapidjson::Value& entry) {
  // Entry must be an object.
  if (!entry.IsObject()) {
    FX_LOGS(ERROR) << "Entry must be an object.";
    return fit::error(ComponentIdIndex::Error::INVALID_SCHEMA);
  }

  // `instance_id` is a required string.
  if (!entry.HasMember("instance_id") || !entry["instance_id"].IsString()) {
    FX_LOGS(ERROR) << "instance_id is a required string.";
    return fit::error(ComponentIdIndex::Error::INVALID_SCHEMA);
  }

  // `instance_id` must be a valid format.
  if (!IsValidInstanceId(entry["instance_id"].GetString())) {
    FX_LOGS(ERROR) << "instance_id must be valid format.";
    return fit::error(ComponentIdIndex::Error::INVALID_INSTANCE_ID);
  }

  // `appmgr_moniker` is a required object.
  if (!entry.HasMember("appmgr_moniker") || !entry["appmgr_moniker"].IsObject()) {
    FX_LOGS(ERROR) << "appmgr_moniker must be valid object.";
    return fit::error(ComponentIdIndex::Error::INVALID_MONIKER);
  }

  const auto& appmgr_moniker = entry["appmgr_moniker"];
  // `url` is a required string.
  if (!appmgr_moniker.HasMember("url") || !appmgr_moniker["url"].IsString()) {
    FX_LOGS(ERROR) << "appmgr_moniker.url is a required string.";
    return fit::error(ComponentIdIndex::Error::INVALID_MONIKER);
  }

  // `realm_path` is a required vector of size >= 1.
  std::vector<std::string> realm_path;
  if (!appmgr_moniker.HasMember("realm_path") ||
      !ParseRealmPath(appmgr_moniker["realm_path"], &realm_path)) {
    FX_LOGS(ERROR) << "appmgr_moniker.realm_path is a required, non-empty list.";
    return fit::error(ComponentIdIndex::Error::INVALID_MONIKER);
  }

  const std::string& component_url = appmgr_moniker["url"].GetString();
  ComponentIdEntry component_id_entry{
      .id = ComponentIdIndex::InstanceId(entry["instance_id"].GetString()),
      .monikers = {Moniker{.url = component_url, .realm_path = std::move(realm_path)}}};

  // 'transitional_realm_paths' is an optional vector of realm paths.
  if (appmgr_moniker.HasMember("transitional_realm_paths")) {
    const auto& transitional_paths = appmgr_moniker["transitional_realm_paths"];
    if (!transitional_paths.IsArray() || transitional_paths.GetArray().Size() < 1) {
      FX_LOGS(ERROR) << "appmgr_moniker.transitional_realm_paths is an optional, non-empty list.";
      return fit::error(ComponentIdIndex::Error::INVALID_MONIKER);
    }

    for (auto& json : transitional_paths.GetArray()) {
      if (!ParseRealmPath(json, &realm_path)) {
        FX_LOGS(ERROR)
            << "appmgr_moniker.transitional_realm_paths entries must be non-empty string lists.";
        return fit::error(ComponentIdIndex::Error::INVALID_MONIKER);
      }

      component_id_entry.monikers.emplace_back(
          Moniker{.url = component_url, .realm_path = std::move(realm_path)});
    }
  }

  return fit::ok(std::move(component_id_entry));
}
}  // namespace

ComponentIdIndex::ComponentIdIndex(ComponentIdIndex::MonikerToInstanceId moniker_to_id,
                                   bool restrict_isolated_persistent_storage)
    : moniker_to_id_(std::move(moniker_to_id)),
      restrict_isolated_persistent_storage_(restrict_isolated_persistent_storage) {}

// static
fit::result<fbl::RefPtr<ComponentIdIndex>, ComponentIdIndex::Error>
ComponentIdIndex::CreateFromAppmgrConfigDir(const fxl::UniqueFD& appmgr_config_dir) {
  if (!files::IsFileAt(appmgr_config_dir.get(), kIndexFilePath)) {
    return fit::ok(fbl::AdoptRef(new ComponentIdIndex({}, false)));
  }

  std::string file_contents;
  if (!files::ReadFileToStringAt(appmgr_config_dir.get(), kIndexFilePath, &file_contents)) {
    FX_LOGS(ERROR) << "Could not read instance ID index file.";
    return fit::error(Error::INVALID_JSON);
  }

  return CreateFromIndexContents(file_contents);
}

// static
fit::result<fbl::RefPtr<ComponentIdIndex>, ComponentIdIndex::Error>
ComponentIdIndex::CreateFromIndexContents(const std::string& index_contents) {
  rapidjson::Document doc;
  doc.Parse(index_contents.c_str());
  if (doc.HasParseError()) {
    FX_LOGS(ERROR) << "Could not json-parse instance ID index file.";
    return fit::error(Error::INVALID_JSON);
  }

  if (!doc.IsObject()) {
    FX_LOGS(ERROR) << "Index must be a valid object.";
    return fit::error(ComponentIdIndex::Error::INVALID_SCHEMA);
  }

  constexpr char kRestrictIsolatedPersistentStorage[] =
      "appmgr_restrict_isolated_persistent_storage";
  // `appmgr_restrict_isolated_persistent_storage` is an optional bool.
  // By default, it is `false`.
  bool restrict_isolated_persistent_storage = false;
  if (doc.HasMember(kRestrictIsolatedPersistentStorage) &&
      // We check that the field is not null, because the compile-time `component_id_index` tool
      // outputs a 'null' valued field when it serializes JSON when it is absent.
      !doc[kRestrictIsolatedPersistentStorage].IsNull()) {
    if (!doc[kRestrictIsolatedPersistentStorage].IsBool()) {
      FX_LOGS(ERROR) << "appmgr_restrict_isolated_persistent_storage must be bool";
      return fit::error(ComponentIdIndex::Error::INVALID_SCHEMA);
    }
    restrict_isolated_persistent_storage = doc[kRestrictIsolatedPersistentStorage].GetBool();
  }

  // `instances` must be an array.
  if (!doc.HasMember("instances") || !doc["instances"].IsArray()) {
    FX_LOGS(ERROR) << "instances is a required list.";
    return fit::error(ComponentIdIndex::Error::INVALID_SCHEMA);
  }

  const auto& instances = doc["instances"].GetArray();
  ComponentIdIndex::MonikerToInstanceId moniker_to_id;
  std::unordered_set<ComponentIdIndex::InstanceId> instance_id_set;
  for (const auto& entry : instances) {
    auto parsed_entry = ParseEntry(entry);
    if (parsed_entry.is_error()) {
      return fit::error(parsed_entry.error());
    }

    auto id_result = instance_id_set.insert(parsed_entry.value().id);
    if (!id_result.second) {
      FX_LOGS(ERROR) << "The set of instance IDs must be unique.";
      return fit::error(ComponentIdIndex::Error::DUPLICATE_INSTANCE_ID);
    }

    for (Moniker& moniker : parsed_entry.value().monikers) {
      auto result =
          moniker_to_id.insert(std::make_pair(std::move(moniker), parsed_entry.value().id));
      if (!result.second) {
        FX_LOGS(ERROR) << "The set of appmgr_monikers must be unique.";
        return fit::error(ComponentIdIndex::Error::DUPLICATE_MONIKER);
      }
    }
  }

  return fit::ok(
      fbl::AdoptRef(new ComponentIdIndex(moniker_to_id, restrict_isolated_persistent_storage)));
}

std::optional<ComponentIdIndex::InstanceId> ComponentIdIndex::LookupMoniker(
    const Moniker& moniker) const {
  const auto& it = moniker_to_id_.find(moniker);
  if (it != moniker_to_id_.end()) {
    return it->second;
  }
  return std::nullopt;
}

}  // namespace component
