// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"

#include "mojo/services/network/url_loader_impl.h"
#include "mojo/services/network/http_client.h"
#include "mojo/services/network/network_error.h"
#include "mojo/services/network/net_adapters.h"

#include <memory>

#include <iostream>
#include <istream>
#include <ostream>
#include <string>

namespace mojo {

URLLoaderImpl::URLLoaderImpl(InterfaceRequest<URLLoader> request)
  : binding_(this, request.Pass()), responded_(false)
{
  binding_.set_connection_error_handler([this]() { OnConnectionError(); });
}

URLLoaderImpl::~URLLoaderImpl() {
}

void URLLoaderImpl::Cleanup() {
  delete this;
}

void URLLoaderImpl::Start(URLRequestPtr request,
                          const Callback<void(URLResponsePtr)>& callback) {
  callback_ = callback;
  StartInternal(request.Pass());
}

void URLLoaderImpl::FollowRedirect(
    const Callback<void(URLResponsePtr)>& callback) {
  NOTIMPLEMENTED();
  callback_ = callback;
  SendError(ERR_NOT_IMPLEMENTED);
}

void URLLoaderImpl::QueryStatus(
    const Callback<void(URLLoaderStatusPtr)>& callback) {
  URLLoaderStatusPtr status(URLLoaderStatus::New());
  NOTIMPLEMENTED();
  status->error = MakeNetworkError(ERR_NOT_IMPLEMENTED);
  callback.Run(status.Pass());
}

void URLLoaderImpl::OnConnectionError() {
  /* TODO(toshik) */
  binding_.Close();
}

void URLLoaderImpl::SendError(int error_code) {
  URLResponsePtr response(URLResponse::New());
  response->error = MakeNetworkError(error_code);
  SendResponse(response.Pass());
}

void URLLoaderImpl::FollowRedirectInternal() {
  /* TODO(toshik) */
}

void URLLoaderImpl::SendResponse(URLResponsePtr response) {
  Callback<void(URLResponsePtr)> callback;
  std::swap(callback_, callback);
  callback.Run(response.Pass());
  responded_ = true;
}

bool URLLoaderImpl::ParseURL(const std::string& url, std::string& proto,
                             std::string& host, std::string& port,
                             std::string& path) {
  std::string delim("://");
  std::string::const_iterator proto_end =
    std::search(url.begin(), url.end(), delim.begin(), delim.end());
  if (proto_end == url.end()) {
    return false;
  }
  proto.assign(url.begin(), proto_end);

  std::string::const_iterator host_start = proto_end + delim.length();
  std::string::const_iterator path_start = std::find(host_start, url.end(), '/');
  std::string::const_iterator host_end = std::find(host_start, path_start, ':');
  host.assign(host_start, host_end);

  if (host_end != path_start)
    port.assign(host_end + 1, path_start);
  else
    port = proto;

  if (path_start != url.end())
    path.assign(path_start, url.end());
  else
    path.assign("/");

  if (proto.length() == 0 || host.length() == 0 || port.length() == 0 ||
      path.length() == 0)
    return false;

  return true;
}

void URLLoaderImpl::StartInternal(URLRequestPtr request) {
  std::string url(request->url);

  asio::io_service io_service;
  bool redirect = false;
  int error_code = ERR_UNEXPECTED;

  do {
    std::string proto, host, port, path;

    if (!ParseURL(url, proto, host, port, path)) {
      std::cout << "url parse error" << std::endl;
      error_code = ERR_INVALID_ARGUMENT;
      break;
    }

    std::cout << "URL: " << host << path << std::endl;
    if (host == "tq.mojoapps.io") {
      std::cout << "rewrote tq.mojoapp.io" << std::endl;
      host = "tq-server";
      port = "80";
      proto = "http";
    }

    if (redirect) {
      io_service.reset();
      redirect = false;
    }

#ifndef NETWORK_SERVICE_USE_HTTPS
    if (proto == "https") {
      std::cerr << "WARNING: network_service was built without HTTPS; "
        "Forcing HTTP instead." << std::endl;
      proto = "http";
      if (port == "443" || port == "https" || port == "") {
        port = "80";
      }
    }
#endif

    if (proto == "https") {
#ifdef NETWORK_SERVICE_USE_HTTPS
      asio::ssl::context ctx(asio::ssl::context::sslv23);
      ctx.set_default_verify_paths();

      HTTPClient<asio::ssl::stream<tcp::socket>>
        c(this, io_service, ctx, host, port, path);
      io_service.run();

      if (c.status_code_ == 301 || c.status_code_ == 302) {
        redirect = true;
        url = c.redirect_location_;
      }
#endif
    } else if (proto == "http") {
      HTTPClient<tcp::socket> c(this, io_service, host, port, path);
      io_service.run();

      if (c.status_code_ == 301 || c.status_code_ == 302) {
        redirect = true;
        url = c.redirect_location_;
        std::cout << "Redirecting to: " << url << std::endl;
      }
    } else {
      // unknown protocol
      error_code = ERR_INVALID_ARGUMENT;
      break;
    }
  } while (redirect);

  if (!responded_)
    SendError(error_code);
}

}  // namespace mojo
