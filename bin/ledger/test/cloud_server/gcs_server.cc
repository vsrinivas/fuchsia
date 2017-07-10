// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/cloud_server/gcs_server.h"

#include "apps/ledger/src/glue/socket/socket_pair.h"
#include "apps/ledger/src/glue/socket/socket_writer.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/vmo/strings.h"
#include "lib/url/gurl.h"

namespace ledger {

GcsServer::GcsServer() {}

GcsServer::~GcsServer() {}

void GcsServer::HandleGet(
    network::URLRequestPtr request,
    const std::function<void(network::URLResponsePtr)> callback) {
  url::GURL url(request->url);

  auto path = url.path();
  if (!data_.count(path)) {
    callback(BuildResponse(request->url, Server::ResponseCode::kNotFound,
                           "No such document."));
    return;
  }

  callback(BuildResponse(request->url, Server::ResponseCode::kOk, data_[path]));
}

void GcsServer::HandlePost(
    network::URLRequestPtr request,
    const std::function<void(network::URLResponsePtr)> callback) {
  url::GURL url(request->url);

  auto path = url.path();
  if (data_.count(path)) {
    callback(BuildResponse(request->url, Server::ResponseCode::kUnauthorized,
                           "Document already exist."));
    return;
  }

  std::string content;
  if (!mtl::StringFromVmo(request->body->get_buffer(), &content)) {
    FTL_NOTREACHED() << "Unable to read vmo.";
  }
  data_[std::move(path)] = std::move(content);
  callback(BuildResponse(request->url, Server::ResponseCode::kOk, "Ok"));
}

}  // namespace ledger
