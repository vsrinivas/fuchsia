// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/testing/server/server.h"

#include <lib/fit/function.h>

#include "lib/fxl/strings/string_number_conversions.h"
#include "peridot/lib/socket/socket_pair.h"
#include "peridot/lib/socket/socket_writer.h"

namespace ledger {

namespace http = ::fuchsia::net::oldhttp;

Server::Server() {}

Server::~Server() {}

void Server::Serve(http::URLRequest request,
                   fit::function<void(http::URLResponse)> callback) {
  FXL_DCHECK(!request.body || request.body->is_sized_buffer());

  if (request.method == "GET") {
    for (const auto& header : *request.headers) {
      if (header.name == "Accept" && header.value == "text/event-stream") {
        HandleGetStream(std::move(request), std::move(callback));
        return;
      }
      if (header.name == "authorization") {
        continue;
      }
      FXL_LOG(WARNING) << "Unknown header: " << header.name << " -> "
                       << header.value;
    }
    HandleGet(std::move(request), std::move(callback));
    return;
  }

  if (request.method == "PATCH") {
    HandlePatch(std::move(request), std::move(callback));
    return;
  }

  if (request.method == "POST") {
    HandlePost(std::move(request), std::move(callback));
    return;
  }

  if (request.method == "PUT") {
    HandlePut(std::move(request), std::move(callback));
    return;
  }

  FXL_NOTREACHED();
}

void Server::HandleGet(http::URLRequest request,
                       fit::function<void(http::URLResponse)> callback) {
  callback(BuildResponse(request.url, ResponseCode::kUnauthorized,
                         "Unauthorized method"));
}

void Server::HandleGetStream(http::URLRequest request,
                             fit::function<void(http::URLResponse)> callback) {
  callback(BuildResponse(request.url, ResponseCode::kUnauthorized,
                         "Unauthorized method"));
}

void Server::HandlePatch(http::URLRequest request,
                         fit::function<void(http::URLResponse)> callback) {
  callback(BuildResponse(request.url, ResponseCode::kUnauthorized,
                         "Unauthorized method"));
}

void Server::HandlePost(http::URLRequest request,
                        fit::function<void(http::URLResponse)> callback) {
  callback(BuildResponse(request.url, ResponseCode::kUnauthorized,
                         "Unauthorized method"));
}

void Server::HandlePut(http::URLRequest request,
                       fit::function<void(http::URLResponse)> callback) {
  callback(BuildResponse(request.url, ResponseCode::kUnauthorized,
                         "Unauthorized method"));
}

http::URLResponse Server::BuildResponse(
    const std::string& url, ResponseCode code, zx::socket body,
    const std::map<std::string, std::string>& headers) {
  http::URLResponse response;
  response.url = url;
  response.status_code = static_cast<uint32_t>(code);
  switch (code) {
    case ResponseCode::kOk:
      response.status_line = "200 OK";
      break;
    case ResponseCode::kUnauthorized:
      response.status_line = "401 Unauthorized";
      break;
    case ResponseCode::kNotFound:
      response.status_line = "404 Not found";
      break;
    default:
      FXL_NOTREACHED();
  }
  for (const auto& pair : headers) {
    http::HttpHeader header;
    header.name = pair.first;
    header.value = pair.second;
    response.headers.push_back(std::move(header));
  }
  if (body) {
    response.body = http::URLBody::New();
    response.body->set_stream(std::move(body));
  }
  return response;
}

http::URLResponse Server::BuildResponse(const std::string& url,
                                        ResponseCode code, std::string body) {
  socket::SocketPair sockets;
  auto* writer = new socket::StringSocketWriter();
  writer->Start(body, std::move(sockets.socket2));
  std::map<std::string, std::string> headers;
  headers["content-length"] = fxl::NumberToString(body.size());
  return BuildResponse(url, code, std::move(sockets.socket1), headers);
}

}  // namespace ledger
