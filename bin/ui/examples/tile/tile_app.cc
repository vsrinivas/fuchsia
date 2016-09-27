// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/tile/tile_app.h"

#include "apps/mozart/examples/tile/tile_view.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"
#include "mojo/public/cpp/application/connect.h"

namespace examples {

TileApp::TileApp() {}

TileApp::~TileApp() {}

void TileApp::CreateView(
    const std::string& connection_url,
    mojo::InterfaceRequest<mojo::ui::ViewOwner> view_owner_request,
    mojo::InterfaceRequest<mojo::ServiceProvider> services) {
  TileParams params;
  if (!ParseParams(connection_url, &params)) {
    FTL_LOG(ERROR) << "Missing or invalid URL parameters.  See README.";
    return;
  }

  new TileView(mojo::CreateApplicationConnector(shell()),
               view_owner_request.Pass(), params);
}

bool TileApp::ParseParams(const std::string& connection_url,
                          TileParams* params) {
  // TODO(jeffbrown): Replace this with a real URL parser.
  size_t query_pos = connection_url.find('?');
  if (query_pos == std::string::npos)
    return true;
  std::string query = connection_url.substr(query_pos + 1);

  for (const auto& pair : ftl::SplitStringCopy(query, "&", ftl::kKeepWhitespace,
                                               ftl::kSplitWantNonEmpty)) {
    size_t value_pos = pair.find('=');
    std::string key = pair.substr(0, value_pos);
    std::string value;
    if (value_pos != std::string::npos)
      value = pair.substr(value_pos + 1);

    if (key == "views") {
      params->view_urls = ftl::SplitStringCopy(value, ",", ftl::kKeepWhitespace,
                                               ftl::kSplitWantNonEmpty);
    } else if (key == "vm") {
      if (value == "any")
        params->version_mode = TileParams::VersionMode::kAny;
      else if (value == "exact")
        params->version_mode = TileParams::VersionMode::kExact;
      else
        return false;
    } else if (key == "cm") {
      if (value == "merge")
        params->combinator_mode = TileParams::CombinatorMode::kMerge;
      else if (value == "prune")
        params->combinator_mode = TileParams::CombinatorMode::kPrune;
      else if (value == "flash")
        params->combinator_mode = TileParams::CombinatorMode::kFallbackFlash;
      else if (value == "dim")
        params->combinator_mode = TileParams::CombinatorMode::kFallbackDim;
      else
        return false;
    } else if (key == "o") {
      if (value == "h")
        params->orientation_mode = TileParams::OrientationMode::kHorizontal;
      else if (value == "v")
        params->orientation_mode = TileParams::OrientationMode::kVertical;
      else
        return false;
    } else {
      return false;
    }
  }
  return !params->view_urls.empty();
}

}  // namespace examples
