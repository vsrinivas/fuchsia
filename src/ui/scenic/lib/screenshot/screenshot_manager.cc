// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screenshot_manager.h"

#include <lib/syslog/cpp/macros.h>

#include "rapidjson/document.h"
#include "screenshot.h"
#include "src/lib/files/file.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

namespace screenshot {
ScreenshotManager::ScreenshotManager(
    std::shared_ptr<flatland::Engine> engine, std::shared_ptr<flatland::VkRenderer> renderer,
    std::shared_ptr<flatland::FlatlandDisplay> display,
    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers)
    : display_width_(1024),
      display_height_(600),
      engine_(engine),
      renderer_(renderer),
      display_(display),
      buffer_collection_importers_(std::move(buffer_collection_importers)) {
  // Check display config.
  std::string display_info_string;
  if (files::ReadFileToString("/config/data/display_info", &display_info_string)) {
    FX_LOGS(INFO) << "Found config file at /config/data/display_info";

    rapidjson::Document document;
    document.Parse(display_info_string);

    if (document.HasMember("width")) {
      auto& val = document["width"];
      FX_CHECK(val.IsInt()) << "display_width must be an integer";
      display_width_ = val.GetInt();
    }

    if (document.HasMember("height")) {
      auto& val = document["height"];
      FX_CHECK(val.IsInt()) << "display_height must be an integer";
      display_height_ = val.GetInt();
    }
  } else {
    FX_LOGS(INFO) << "No config file found at /config/data/display_info; using default values";
    display_width_ = 1024;
    display_height_ = 600;
  }
}

void ScreenshotManager::CreateClient(
    fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot> request) {
  const auto id = next_client_id_++;

  std::unique_ptr<Screenshot> screenshot = std::make_unique<Screenshot>(
      std::move(request), display_width_, display_height_, buffer_collection_importers_, renderer_,
      [this]() { return engine_->GetRenderables(*(display_.get())); });

  screenshot_clients_[id] = std::move(screenshot);
}

}  // namespace screenshot
