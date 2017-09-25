// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/cloud_server/gcs_server.h"

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/url/gurl.h"
#include "peridot/bin/ledger/glue/socket/socket_pair.h"
#include "peridot/bin/ledger/glue/socket/socket_writer.h"

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
  // Do not verify whether the object already exists - the real Firebase Storage
  // doesn't do that either.

  std::string content;
  if (!fsl::StringFromVmo(request->body->get_buffer(), &content)) {
    FXL_NOTREACHED() << "Unable to read vmo.";
  }
  data_[std::move(path)] = std::move(content);
  callback(BuildResponse(request->url, Server::ResponseCode::kOk, "Ok"));
}

}  // namespace ledger
