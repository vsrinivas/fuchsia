// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/ranking_feature.h"

#include <lib/fidl/cpp/clone.h>
#include "src/lib/files/file.h"
#include <src/lib/fxl/logging.h>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"

namespace modular {

int RankingFeature::instances_ = 0;

RankingFeature::RankingFeature() : id_(instances_++) {}

RankingFeature::~RankingFeature() = default;

double RankingFeature::ComputeFeature(const fuchsia::modular::UserInput& query,
                                      const RankedSuggestion& suggestion) {
  const double feature = ComputeFeatureInternal(query, suggestion);
  FXL_CHECK(feature <= kMaxConfidence);
  FXL_CHECK(feature >= kMinConfidence);
  return feature;
}

fuchsia::modular::ContextSelectorPtr RankingFeature::CreateContextSelector() {
  return CreateContextSelectorInternal();
}

void RankingFeature::UpdateContext(
    const std::vector<fuchsia::modular::ContextValue>& context_update_values) {
  context_values_.reset(fidl::Clone(context_update_values));
}

fuchsia::modular::ContextSelectorPtr
RankingFeature::CreateContextSelectorInternal() {
  // By default we return a nullptr, meaning that the ranking feature doesn't
  // require context. If a ranking feature requires context, it should create a
  // context selector, set the values it needs and return it.
  return nullptr;
}

fidl::VectorPtr<fuchsia::modular::ContextValue>&
RankingFeature::ContextValues() {
  return context_values_;
}

std::optional<rapidjson::Document> RankingFeature::FetchJsonObject(
    const std::string& path) {
  // Load data file to string.
  std::string data;
  if (!files::ReadFileToString(path, &data)) {
    FXL_LOG(WARNING) << "Missing ranking feature data file: " << path;
    return std::nullopt;
  }

  // Parse json data string.
  rapidjson::Document data_doc;
  data_doc.Parse(data);
  if (data_doc.HasParseError()) {
    FXL_LOG(WARNING) << "Invalid JSON (" << path << " at "
                     << data_doc.GetErrorOffset() << "): "
                     << rapidjson::GetParseError_En(data_doc.GetParseError());
    return std::nullopt;
  }

  return std::move(data_doc);
}

}  // namespace modular
