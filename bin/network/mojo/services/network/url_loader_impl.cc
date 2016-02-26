// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"

#include "mojo/services/network/url_loader_impl.h"
#include "mojo/services/network/http_client.h"
#include "mojo/services/network/net_errors.h"
#include "mojo/services/network/net_adapters.h"

#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <memory>

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
  NOTIMPLEMENTED();
  callback_ = callback;
  SendError(net::ERR_NOT_IMPLEMENTED);
}

void URLLoaderImpl::QueryStatus(
    const Callback<void(URLLoaderStatusPtr)>& callback) {
  URLLoaderStatusPtr status(URLLoaderStatus::New());
  NOTIMPLEMENTED();
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

  std::unique_ptr<URL> url(new URL(url_str));
  if (!url->IsParsed()) {
    LOG(ERROR) << "url parse error";
    SendError(net::ERR_INVALID_ARGUMENT);
    return;
  }

  do {
    if (redirect) {
      io_service.reset();
      redirect = false;
    }

    if (url->Proto() == "https") {
#ifdef NETWORK_SERVICE_USE_HTTPS
      asio::ssl::context ctx(asio::ssl::context::sslv23);
      ctx.set_default_verify_paths();

      HTTPClient<asio::ssl::stream<tcp::socket>> c(this, io_service, ctx);
      MojoResult result = c.CreateRequest(url->Host(), url->Path(), method,
                                          extra_headers, element_readers);
      if (result != MOJO_RESULT_OK) {
        SendError(net::ERR_INVALID_ARGUMENT);
        break;
      }
      c.Start(url->Host(), url->Port());
      io_service.run();

      if (c.status_code_ == 301 || c.status_code_ == 302) {
        redirect = true;
        url.reset(new URL(c.redirect_location_));
        if (!url->IsParsed()) {
          LOG(ERROR) << "url parse error";
          SendError(net::ERR_INVALID_RESPONSE);
          break;
        }
      }
#else
      LOG(INFO) << "https is not built-in. "
        "please build with NETWORK_SERVICE_USE_HTTPS";
      SendError(net::ERR_INVALID_ARGUMENT);
      break;
#endif
    } else if (url->Proto() == "http") {
      HTTPClient<tcp::socket> c(this, io_service);
      MojoResult result = c.CreateRequest(url->Host(), url->Path(), method,
                                          extra_headers, element_readers);
      if (result != MOJO_RESULT_OK) {
        SendError(net::ERR_INVALID_ARGUMENT);
        break;
      }
      c.Start(url->Host(), url->Port());
      io_service.run();

      if (c.status_code_ == 301 || c.status_code_ == 302) {
        redirect = true;
        url.reset(new URL(c.redirect_location_));
        if (!url->IsParsed()) {
          LOG(ERROR) << "url parse error";
          SendError(net::ERR_INVALID_RESPONSE);
          break;
        }
      }
    } else {
      // unknown protocol
      LOG(ERROR) << "unknown protocol";
      SendError(net::ERR_INVALID_ARGUMENT);
      break;
    }
  } while (redirect);
}

}  // namespace mojo
