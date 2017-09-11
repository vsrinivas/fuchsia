// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/component_manager/resource_loader.h"

#include <regex>

#include "apps/modular/src/component_manager/make_network_error.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/vmo/file.h"
#include "lib/mtl/vmo/strings.h"

namespace component {

namespace {

constexpr char kLocalComponentsPath[] = "/system/components/";

std::string PathForUrl(const std::string& url) {
  std::regex re("[:/]+");
  return files::SimplifyPath(kLocalComponentsPath +
                             std::regex_replace(url, re, "/"));
}

}  // namespace

ResourceLoader::ResourceLoader(network::NetworkServicePtr network_service)
    : network_service_(std::move(network_service)) {
  network_service_.set_connection_error_handler(
      [] { FXL_LOG(ERROR) << "Error from network service connection"; });
}

void ResourceLoader::LoadResource(const std::string& url,
                                  const Callback& callback_) {
  const Callback& callback(callback_);
  mx::vmo vmo;

  // Look in the local components directory.
  auto local_path = PathForUrl(url);
  if (files::IsFile(local_path)) {
    // Found locally.
    if (mtl::VmoFromFilename(local_path, &vmo)) {
      callback(std::move(vmo), nullptr);
      return;
    }
    // Log a warning and fall back to loading from the network.
    FXL_LOG(WARNING) << "Error reading " << local_path << " into VMO.";
  }

  // Load from the network.
  network::URLLoaderPtr url_loader;
  network_service_->CreateURLLoader(url_loader.NewRequest());
  url_loader.set_connection_error_handler([callback] {
    FXL_LOG(ERROR) << "Error from URLLoader connection";
    callback(mx::vmo(), MakeNetworkError(500, "URLLoader channel closed"));
  });

  network::URLRequestPtr request = network::URLRequest::New();
  request->response_body_mode = network::URLRequest::ResponseBodyMode::BUFFER;
  request->url = url;

  url_loader->Start(
      std::move(request), fxl::MakeCopyable([
        this, url, callback, url_loader = std::move(url_loader)
      ](network::URLResponsePtr response) mutable {
        if (response->error) {
          FXL_LOG(ERROR) << "URL response contained error: "
                         << response->error->description;
          callback(mx::vmo(), std::move(response->error));
          return;
        }

        if (response->body->is_buffer()) {
          // If the network service returned a vmo, pass that off.
          callback(std::move(response->body->get_buffer()), nullptr);
          return;
        }

        // TODO(ianloic): work out if we can drain a stream into a vmo
        // directly.

        // If the network service returned a stream, drain it to a string.
        std::string data;
        if (!mtl::BlockingCopyToString(std::move(response->body->get_stream()),
                                       &data)) {
          FXL_LOG(ERROR) << "Failed to read URL response stream.";
          callback(mx::vmo(), MakeNetworkError(
                                  500, "Failed to read URL response stream."));
          return;
        }

        // Copy the string into a VMO.
        mx::vmo vmo;
        if (!mtl::VmoFromString(data, &vmo)) {
          FXL_LOG(ERROR) << "Failed to get vmo from string";
          callback(mx::vmo(),
                   MakeNetworkError(500, "Failed to make vmo from string"));
          return;
        }

        callback(std::move(vmo), nullptr);
      }));
}

}  // namespace component
