// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/index.h"

#include <algorithm>
#include <sstream>

#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <src/lib/fxl/logging.h>

namespace modular {

ContextIndex::ContextIndex() = default;
ContextIndex::~ContextIndex() = default;

namespace internal {
// Keys for fields within fuchsia::modular::ContextMetadata.story:
const char kStoryIdKey[] = "si";
const char kStoryFocusedKey[] = "sf";

// Keys for fields within fuchsia::modular::ContextMetadata.mod:
const char kModPathKey[] = "mp";
const char kModUrlKey[] = "mu";
const char kModuleFocusedKey[] = "mf";

// Keys for fields within fuchsia::modular::ContextMetadata.entity:
const char kEntityTopicKey[] = "et";
const char kEntityTypeKey[] = "ey";
// We don't index |ctime|.

// Key for fuchsia::modular::ContextValueType.
const char kContextValueTypeKey[] = "t";

std::set<std::string> EncodeMetadataAndType(
    fuchsia::modular::ContextValueType node_type,
    const fuchsia::modular::ContextMetadata& metadata) {
  fuchsia::modular::ContextMetadata meta_clone;
  fidl::Clone(metadata, &meta_clone);
  return EncodeMetadataAndType(node_type,
                               fidl::MakeOptional(std::move(meta_clone)));
}

std::string EncodeFocus(const std::string& key,
                        fuchsia::modular::FocusedStateState focused_state) {
  std::ostringstream str;
  str << key;
  if (focused_state == fuchsia::modular::FocusedStateState::FOCUSED) {
    str << "1";
  } else {
    str << "0";
  }
  return str.str();
}

std::set<std::string> EncodeMetadataAndType(
    fuchsia::modular::ContextValueType node_type,
    const fuchsia::modular::ContextMetadataPtr& metadata) {
  std::set<std::string> ret;

  if (metadata) {
    if (metadata->story) {
      if (metadata->story->id) {
        std::ostringstream str;
        str << kStoryIdKey << metadata->story->id;
        ret.insert(str.str());
      }
      if (metadata->story->focused) {
        ret.insert(
            EncodeFocus(kStoryFocusedKey, metadata->story->focused->state));
      }
    }

    if (metadata->mod) {
      if (metadata->mod->url) {
        std::ostringstream str;
        str << kModUrlKey << metadata->mod->url;
        ret.insert(str.str());
      }
      if (metadata->mod->path) {
        std::ostringstream str;
        str << kModPathKey;
        for (const auto& part : *metadata->mod->path) {
          str << '\0' << part;
        }
        ret.insert(str.str());
      }
      if (metadata->mod->focused) {
        ret.insert(
            EncodeFocus(kModuleFocusedKey, metadata->mod->focused->state));
      }
    }

    if (metadata->entity) {
      if (metadata->entity->topic) {
        std::ostringstream str;
        str << kEntityTopicKey << metadata->entity->topic;
        ret.insert(str.str());
      }
      if (metadata->entity->type) {
        for (const auto& type : *metadata->entity->type) {
          std::ostringstream str;
          str << kEntityTypeKey << type;
          ret.insert(str.str());
        }
      }
    }
  }

  std::ostringstream str;
  str << kContextValueTypeKey << fidl::ToUnderlying(node_type);
  ret.insert(str.str());

  return ret;
}

}  // namespace internal

void ContextIndex::Add(Id id, fuchsia::modular::ContextValueType type,
                       const fuchsia::modular::ContextMetadata& metadata) {
  auto keys = internal::EncodeMetadataAndType(type, metadata);
  for (const auto& key : keys) {
    index_[key].insert(id);
  }
}

void ContextIndex::Remove(Id id, fuchsia::modular::ContextValueType type,
                          const fuchsia::modular::ContextMetadata& metadata) {
  auto keys = internal::EncodeMetadataAndType(type, metadata);
  for (const auto& key : keys) {
    index_[key].erase(id);
  }
}

void ContextIndex::Query(fuchsia::modular::ContextValueType type,
                         const fuchsia::modular::ContextMetadataPtr& metadata,
                         std::set<ContextIndex::Id>* out) {
  FXL_DCHECK(out != nullptr);

  auto keys = internal::EncodeMetadataAndType(type, metadata);
  std::set<ContextIndex::Id> ret;
  if (keys.empty())
    return;
  const auto& first = index_[*keys.begin()];
  ret.insert(first.begin(), first.end());
  keys.erase(keys.begin());

  for (const auto& key : keys) {
    std::set<ContextIndex::Id> intersection;
    const auto& posting_list = index_[key];
    std::set_intersection(ret.begin(), ret.end(), posting_list.begin(),
                          posting_list.end(),
                          std::inserter(intersection, intersection.begin()));
    ret.swap(intersection);
  }

  out->insert(ret.begin(), ret.end());
}

}  // namespace modular
