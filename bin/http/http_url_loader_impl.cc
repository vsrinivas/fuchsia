// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "http_url_loader_impl.h"

#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "garnet/bin/http/http_client.h"
#include "garnet/bin/http/http_adapters.h"
#include "garnet/bin/http/http_errors.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/url/gurl.h"

namespace http {

namespace oldhttp = ::fuchsia::net::oldhttp;

namespace {
const size_t kMaxRedirects = 20;
}  // namespace

URLLoaderImpl::URLLoaderImpl(Coordinator* coordinator)
    : coordinator_(coordinator) {}

URLLoaderImpl::~URLLoaderImpl() {}

void URLLoaderImpl::Start(oldhttp::URLRequest request, Callback callback) {
  callback_ = std::move(callback);
  coordinator_->RequestNetworkSlot(fxl::MakeCopyable(
      [ this, request = std::move(request) ](fit::closure on_inactive) mutable {
        StartInternal(std::move(request));
        on_inactive();
      }));
}

void URLLoaderImpl::FollowRedirect(Callback callback) {
  FXL_NOTIMPLEMENTED();
  callback_ = std::move(callback);
  SendError(HTTP_ERR_NOT_IMPLEMENTED);
}

void URLLoaderImpl::QueryStatus(QueryStatusCallback callback) {
  oldhttp::URLLoaderStatus status;
  FXL_NOTIMPLEMENTED();
  status.error = MakeHttpError(HTTP_ERR_NOT_IMPLEMENTED);
  callback(std::move(status));
}

void URLLoaderImpl::SendError(int error_code) {
  oldhttp::URLResponse response;
  response.error = MakeHttpError(error_code);
  if (current_url_.is_valid()) {
    response.url = current_url_.spec();
  }
  SendResponse(std::move(response));
}

void URLLoaderImpl::FollowRedirectInternal() {
  /* TODO(toshik) */
}

void URLLoaderImpl::SendResponse(oldhttp::URLResponse response) {
  Callback callback;
  std::swap(callback_, callback);
  callback(std::move(response));
}

void URLLoaderImpl::StartInternal(oldhttp::URLRequest request) {
  std::string url_str = request.url;
  std::string method = request.method;
  std::map<std::string, std::string> extra_headers;
  std::unique_ptr<http::UploadElementReader> request_body_reader;

  if (request.headers) {
    for (size_t i = 0; i < request.headers->size(); ++i)
      extra_headers[request.headers->at(i).name] =
          request.headers->at(i).value;
  }

  if (request.body) {
    // TODO(kulakowski) Implement responses into a shared_buffer
    if (request.body->is_stream()) {
      request_body_reader = std::make_unique<http::SocketUploadElementReader>(
          std::move(request.body->stream()));
    } else if (request.body->is_buffer()) {
      request_body_reader = std::make_unique<http::VmoUploadElementReader>(
          std::move(request.body->buffer()));
    } else {
      FXL_DCHECK(request.body->is_sized_buffer());
      request_body_reader = std::make_unique<http::VmoUploadElementReader>(
          std::move(request.body->sized_buffer().vmo),
          request.body->sized_buffer().size);
    }
  }

  response_body_mode_ = request.response_body_mode;

  asio::io_service io_service;
  size_t redirectsLeft = kMaxRedirects;

  current_url_ = url::GURL(url_str);
  if (!current_url_.is_valid()) {
    SendError(HTTP_ERR_INVALID_ARGUMENT);
    return;
  }

  do {
    if (current_url_.SchemeIs("https")) {
#ifdef NETWORK_SERVICE_USE_HTTPS
      asio::ssl::context ctx(asio::ssl::context::sslv23);
#ifndef NETWORK_SERVICE_DISABLE_CERT_VERIFY
      ctx.set_default_verify_paths();
#endif
      HTTPClient<asio::ssl::stream<tcp::socket>> c(this, io_service, ctx);
      zx_status_t result = c.CreateRequest(
          current_url_.host(),
          current_url_.path() +
              (current_url_.has_query() ? "?" + current_url_.query() : ""),
          method, extra_headers, std::move(request_body_reader));
      if (result != ZX_OK) {
        SendError(HTTP_ERR_INVALID_ARGUMENT);
        return;
      }
      c.Start(current_url_.host(),
              current_url_.has_port() ? current_url_.port() : "https");
      io_service.run();

      if (c.status_code_ == 301 || c.status_code_ == 302) {
        current_url_ = url::GURL(c.redirect_location_);
        if (!current_url_.is_valid()) {
          SendError(HTTP_ERR_INVALID_RESPONSE);
          return;
        }
        // Follow redirect
        io_service.reset();
        continue;
      }
#else
      FXL_LOG(WARNING) << "https is not built-in. "
                          "please build with NETWORK_SERVICE_USE_HTTPS";
      SendError(HTTP_ERR_INVALID_ARGUMENT);
      return;
#endif
    } else if (current_url_.SchemeIs("http")) {
      HTTPClient<tcp::socket> c(this, io_service);
      zx_status_t result = c.CreateRequest(
          current_url_.host(),
          current_url_.path() +
              (current_url_.has_query() ? "?" + current_url_.query() : ""),
          method, extra_headers, std::move(request_body_reader));
      if (result != ZX_OK) {
        SendError(HTTP_ERR_INVALID_ARGUMENT);
        return;
      }
      c.Start(current_url_.host(),
              current_url_.has_port() ? current_url_.port() : "http");
      io_service.run();

      if (c.status_code_ == 301 || c.status_code_ == 302) {
        current_url_ = url::GURL(c.redirect_location_);
        if (!current_url_.is_valid()) {
          SendError(HTTP_ERR_INVALID_RESPONSE);
          return;
        }
        // Follow redirect
        io_service.reset();
        continue;
      }
    } else {
      // unknown protocol
      SendError(HTTP_ERR_INVALID_ARGUMENT);
      return;
    }
    // Success without redirect
    return;
  } while (--redirectsLeft);
  SendError(HTTP_ERR_TOO_MANY_REDIRECTS);
}

}  // namespace http
