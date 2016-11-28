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
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/url/gurl.h"

namespace network {

URLLoaderImpl::URLLoaderImpl(Coordinator* coordinator)
    : coordinator_(coordinator) {}

URLLoaderImpl::~URLLoaderImpl() {}

void URLLoaderImpl::Start(URLRequestPtr request, const Callback& callback) {
  callback_ = std::move(callback);
  coordinator_->RequestNetworkSlot(ftl::MakeCopyable(
      [ this, request = std::move(request) ](ftl::Closure on_inactive) mutable {
        StartInternal(std::move(request));
        on_inactive();
      }));
}

void URLLoaderImpl::FollowRedirect(const Callback& callback) {
  FTL_NOTIMPLEMENTED();
  callback_ = callback;
  SendError(network::NETWORK_ERR_NOT_IMPLEMENTED);
}

void URLLoaderImpl::QueryStatus(const QueryStatusCallback& callback) {
  URLLoaderStatusPtr status(URLLoaderStatus::New());
  FTL_NOTIMPLEMENTED();
  status->error = MakeNetworkError(network::NETWORK_ERR_NOT_IMPLEMENTED);
  callback(std::move(status));
}

void URLLoaderImpl::SendError(int error_code) {
  URLResponsePtr response(URLResponse::New());
  response->error = MakeNetworkError(error_code);
  if (current_url_.is_valid()) {
    response->url = current_url_.spec();
  }
  SendResponse(std::move(response));
}

void URLLoaderImpl::FollowRedirectInternal() {
  /* TODO(toshik) */
}

void URLLoaderImpl::SendResponse(URLResponsePtr response) {
  Callback callback;
  std::swap(callback_, callback);
  callback(std::move(response));
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
    // TODO(kulakowski) Implement responses into a shared_buffer
    if (request->body->is_stream()) {
      element_readers.push_back(std::make_unique<DatapipeUploadElementReader>(
          std::move(request->body->get_stream())));
    } else {
      element_readers.push_back(std::make_unique<VmoUploadElementReader>(
          std::move(request->body->get_buffer())));
    }
  }

  asio::io_service io_service;
  bool redirect = false;

  current_url_ = url::GURL(url_str);
  if (!current_url_.is_valid()) {
    FTL_LOG(ERROR) << "url parse error";
    SendError(network::NETWORK_ERR_INVALID_ARGUMENT);
    return;
  }

  do {
    if (redirect) {
      io_service.reset();
      redirect = false;
    }

    if (current_url_.SchemeIs("https")) {
#ifdef NETWORK_SERVICE_USE_HTTPS
      asio::ssl::context ctx(asio::ssl::context::sslv23);
      ctx.set_default_verify_paths();

      HTTPClient<asio::ssl::stream<tcp::socket>> c(this, io_service, ctx);
      mx_status_t result = c.CreateRequest(
          current_url_.host(),
          current_url_.path() +
              (current_url_.has_query() ? "?" + current_url_.query() : ""),
          method, extra_headers, element_readers);
      if (result != NO_ERROR) {
        SendError(network::NETWORK_ERR_INVALID_ARGUMENT);
        break;
      }
      c.Start(current_url_.host(),
              current_url_.has_port() ? current_url_.port() : "https");
      io_service.run();

      if (c.status_code_ == 301 || c.status_code_ == 302) {
        redirect = true;
        current_url_ = url::GURL(c.redirect_location_);
        if (!current_url_.is_valid()) {
          FTL_LOG(ERROR) << "url parse error";
          SendError(network::NETWORK_ERR_INVALID_RESPONSE);
          break;
        }
      }
#else
      FTL_LOG(INFO) << "https is not built-in. "
                       "please build with NETWORK_SERVICE_USE_HTTPS";
      SendError(network::NETWORK_ERR_INVALID_ARGUMENT);
      break;
#endif
    } else if (current_url_.SchemeIs("http")) {
      HTTPClient<tcp::socket> c(this, io_service);
      mx_status_t result = c.CreateRequest(
          current_url_.host(),
          current_url_.path() +
              (current_url_.has_query() ? "?" + current_url_.query() : ""),
          method, extra_headers, element_readers);
      if (result != NO_ERROR) {
        SendError(network::NETWORK_ERR_INVALID_ARGUMENT);
        break;
      }
      c.Start(current_url_.host(),
              current_url_.has_port() ? current_url_.port() : "http");
      io_service.run();

      if (c.status_code_ == 301 || c.status_code_ == 302) {
        redirect = true;
        current_url_ = url::GURL(c.redirect_location_);
        if (!current_url_.is_valid()) {
          FTL_LOG(ERROR) << "url parse error";
          SendError(network::NETWORK_ERR_INVALID_RESPONSE);
          break;
        }
      }
    } else {
      // unknown protocol
      FTL_LOG(ERROR) << "unknown protocol";
      SendError(network::NETWORK_ERR_INVALID_ARGUMENT);
      break;
    }
  } while (redirect);
}

}  // namespace network
