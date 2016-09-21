// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "url_loader_impl.h"

#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "apps/network/http_client.h"
#include "apps/network/net_adapters.h"
#include "apps/network/net_errors.h"
#include "lib/ftl/logging.h"
#include "url/gurl.h"

namespace mojo {

URLLoaderImpl::URLLoaderImpl(InterfaceRequest<URLLoader> request)
  : binding_(this, request.Pass())
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
  FTL_NOTIMPLEMENTED();
  callback_ = callback;
  SendError(net::ERR_NOT_IMPLEMENTED);
}

void URLLoaderImpl::QueryStatus(
    const Callback<void(URLLoaderStatusPtr)>& callback) {
  URLLoaderStatusPtr status(URLLoaderStatus::New());
  FTL_NOTIMPLEMENTED();
  status->error = MakeNetworkError(net::ERR_NOT_IMPLEMENTED);
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
}

void URLLoaderImpl::StartInternal(URLRequestPtr request) {
  std::string url_str(request->url);
  std::string method(request->method);
  std::map<std::string, std::string> extra_headers;
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;

  if (request->headers) {
    for (size_t i = 0; i < request->headers.size(); ++i)
      extra_headers[request->headers[i]->name] = request->headers[i]->value;
  }

  if (request->body) {
    for (size_t i = 0; i < request->body.size(); ++i)
      element_readers.push_back(
          std::unique_ptr<UploadElementReader>(
              new UploadElementReader(request->body[i].Pass())));
  }

  asio::io_service io_service;
  bool redirect = false;

  GURL url(url_str);
  if (!url.is_valid()) {
    FTL_LOG(ERROR) << "url parse error";
    SendError(net::ERR_INVALID_ARGUMENT);
    return;
  }

  do {
    if (redirect) {
      io_service.reset();
      redirect = false;
    }

    if (url.SchemeIs("https")) {
#ifdef NETWORK_SERVICE_USE_HTTPS
      asio::ssl::context ctx(asio::ssl::context::sslv23);
      ctx.set_default_verify_paths();

      HTTPClient<asio::ssl::stream<tcp::socket>> c(this, io_service, ctx);
      MojoResult result = c.CreateRequest(url.host(), url.path(), method,
                                          extra_headers, element_readers);
      if (result != MOJO_RESULT_OK) {
        SendError(net::ERR_INVALID_ARGUMENT);
        break;
      }
      c.Start(url.host(), url.has_port() ? url.port() : "https");
      io_service.run();

      if (c.status_code_ == 301 || c.status_code_ == 302) {
        redirect = true;
        url = GURL(c.redirect_location_);
        if (!url.is_valid()) {
          FTL_LOG(ERROR) << "url parse error";
          SendError(net::ERR_INVALID_RESPONSE);
          break;
        }
      }
#else
      FTL_LOG(INFO) << "https is not built-in. "
        "please build with NETWORK_SERVICE_USE_HTTPS";
      SendError(net::ERR_INVALID_ARGUMENT);
      break;
#endif
    } else if (url.SchemeIs("http")) {
      HTTPClient<tcp::socket> c(this, io_service);
      MojoResult result = c.CreateRequest(url.host(), url.path(), method,
                                          extra_headers, element_readers);
      if (result != MOJO_RESULT_OK) {
        SendError(net::ERR_INVALID_ARGUMENT);
        break;
      }
      c.Start(url.host(), url.has_port() ? url.port() : "http");
      io_service.run();

      if (c.status_code_ == 301 || c.status_code_ == 302) {
        redirect = true;
        url = GURL(c.redirect_location_);
        if (!url.is_valid()) {
          FTL_LOG(ERROR) << "url parse error";
          SendError(net::ERR_INVALID_RESPONSE);
          break;
        }
      }
    } else {
      // unknown protocol
      FTL_LOG(ERROR) << "unknown protocol";
      SendError(net::ERR_INVALID_ARGUMENT);
      break;
    }
  } while (redirect);
}

}  // namespace mojo
