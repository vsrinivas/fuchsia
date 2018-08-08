// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HTTP_HTTP_NEW_CLIENT_H_
#define GARNET_BIN_HTTP_HTTP_NEW_CLIENT_H_

#include <zircon/status.h>

#include <set>

#include "garnet/bin/http/http_errors.h"
#include "garnet/bin/http/upload_element_reader.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/ascii.h"

#include <asio.hpp>
#include <asio/ssl.hpp>

using asio::ip::tcp;

namespace http {

typedef asio::ssl::stream<tcp::socket> ssl_socket_t;
typedef tcp::socket nonssl_socket_t;

template <typename T>
class URLLoaderImpl::HTTPClient {
  static_assert(std::is_same<T, ssl_socket_t>::value ||
                    std::is_same<T, nonssl_socket_t>::value,
                "requires either ssl_socket_t or nonssl_socket_t");

 public:
  static const std::set<std::string> ALLOWED_METHODS;

  static bool IsMethodAllowed(const std::string& method);

  HTTPClient<T>(URLLoaderImpl* loader, asio::io_service& io_service,
                asio::ssl::context& context);

  HTTPClient<T>(URLLoaderImpl* loader, asio::io_service& io_service);

  zx_status_t CreateRequest(
      const std::string& server, const std::string& path,
      const std::string& method,
      const std::map<std::string, std::string>& extra_headers,
      std::unique_ptr<http::UploadElementReader> request_body_reader);
  void Start(const std::string& server, const std::string& port);

 private:
  using TransferBuffer = char[64 * 1024];

  void SetHostName(const std::string& server);

  void OnResolve(const asio::error_code& err,
                 tcp::resolver::iterator endpoint_iterator);
  bool OnVerifyCertificate(bool preverified, asio::ssl::verify_context& ctx);
  void OnConnect(const asio::error_code& err);
  void OnHandShake(const asio::error_code& err);
  void OnWriteRequestHeaders(const asio::error_code& err,
                             std::size_t transferred);
  void WriteRequestBody();
  void OnWriteRequestBody(const asio::error_code& err, std::size_t transferred);
  void OnReadStatusLine(const asio::error_code& err);
  zx_status_t SendStreamedBody();
  zx_status_t SendBufferedBody();
  void ParseHeaderField(const std::string& header, std::string* name,
                        std::string* value);
  void OnReadHeaders(const asio::error_code& err);
  void OnStreamBody(const asio::error_code& err);
  void OnBufferBody(const asio::error_code& err);

  void SendResponse(::fuchsia::net::oldhttp::URLResponse response);
  void SendError(int error_code);

 public:
  unsigned int status_code_;
  std::string redirect_location_;

 private:
  URLLoaderImpl* loader_;

  tcp::resolver resolver_;
  T socket_;
  asio::streambuf request_header_buf_;
  std::unique_ptr<http::UploadElementReader> request_body_reader_;
  asio::streambuf request_body_buf_;
  std::ostream request_body_stream_;
  asio::streambuf response_buf_;

  std::string http_version_;
  std::string status_message_;

  ::fuchsia::net::oldhttp::URLResponse
      response_;                     // used for buffered responses
  zx::socket response_body_stream_;  // used for streamed responses (default)
};

template <typename T>
const std::set<std::string> URLLoaderImpl::HTTPClient<T>::ALLOWED_METHODS{
    "GET", "HEAD", "POST", "PUT", "DELETE", "TRACE", "CONNECT", "PATCH"};

template <typename T>
bool URLLoaderImpl::HTTPClient<T>::IsMethodAllowed(const std::string& method) {
  return ALLOWED_METHODS.find(method) != ALLOWED_METHODS.end();
}

template <>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::OnResolve(
    const asio::error_code& err, tcp::resolver::iterator endpoint_iterator);
template <>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::OnResolve(
    const asio::error_code& err, tcp::resolver::iterator endpoint_iterator);
template <>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::OnConnect(
    const asio::error_code& err);
template <>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::OnConnect(
    const asio::error_code& err);

template <>
URLLoaderImpl::HTTPClient<ssl_socket_t>::HTTPClient(
    URLLoaderImpl* loader, asio::io_service& io_service,
    asio::ssl::context& context)
    : loader_(loader),
      resolver_(io_service),
      socket_(io_service, context),
      request_body_stream_(&request_body_buf_) {}

template <>
URLLoaderImpl::HTTPClient<nonssl_socket_t>::HTTPClient(
    URLLoaderImpl* loader, asio::io_service& io_service)
    : loader_(loader),
      resolver_(io_service),
      socket_(io_service),
      request_body_stream_(&request_body_buf_) {}

template <typename T>
zx_status_t URLLoaderImpl::HTTPClient<T>::CreateRequest(
    const std::string& server, const std::string& path,
    const std::string& method,
    const std::map<std::string, std::string>& extra_headers,
    std::unique_ptr<http::UploadElementReader> request_body_reader) {
  if (!IsMethodAllowed(method)) {
    FXL_VLOG(1) << "Method " << method << " is not allowed";
    return ZX_ERR_INVALID_ARGS;
  }

  SetHostName(server);

  std::ostream request_header_stream(&request_header_buf_);

  bool has_accept = false;
  request_header_stream << method << " " << path << " HTTP/1.1\r\n";
  request_header_stream << "Host: " << server << "\r\n";
  // TODO(toshik): should we make this work without closing the connection?
  request_header_stream << "Connection: close\r\n";

  for (auto it = extra_headers.begin(); it != extra_headers.end(); ++it) {
    request_header_stream << it->first << ": " << it->second << "\r\n";
    has_accept =
        has_accept || fxl::EqualsCaseInsensitiveASCII(it->first, "accept");
  }
  if (!has_accept)
    request_header_stream << "Accept: */*\r\n";

  request_body_reader_ = std::move(request_body_reader);
  if (request_body_reader_) {
    size_t content_length = request_body_reader_->size();
    if (request_body_reader_->err() != ZX_OK) {
      return request_body_reader_->err();
    }
    if (content_length != http::UploadElementReader::kUnknownSize) {
      request_header_stream << "Content-Length: " << content_length << "\r\n";
    }
  }

  request_header_stream << "\r\n";

  return ZX_OK;
}

template <>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::SetHostName(
    const std::string& server) {
  ::SSL_set_tlsext_host_name(socket_.native_handle(), server.c_str());
  asio::detail::throw_error(asio::error_code(), "set_tlsext_host_name");
}

template <>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::SetHostName(
    const std::string& server) {}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::Start(const std::string& server,
                                         const std::string& port) {
  tcp::resolver::query query(server, port);
  resolver_.async_resolve(
      query, std::bind(&HTTPClient<T>::OnResolve, this, std::placeholders::_1,
                       std::placeholders::_2));
}

template <>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::OnResolve(
    const asio::error_code& err, tcp::resolver::iterator endpoint_iterator) {
  if (!err) {
#ifdef NETWORK_SERVICE_DISABLE_CERT_VERIFY
    socket_.set_verify_mode(asio::ssl::verify_none);
#else
    socket_.set_verify_mode(asio::ssl::verify_peer);
#endif
    socket_.set_verify_callback(
        std::bind(&HTTPClient<ssl_socket_t>::OnVerifyCertificate, this,
                  std::placeholders::_1, std::placeholders::_2));
    asio::async_connect(socket_.lowest_layer(), endpoint_iterator,
                        std::bind(&HTTPClient<ssl_socket_t>::OnConnect, this,
                                  std::placeholders::_1));
  } else {
    FXL_VLOG(1) << "Resolve(SSL): " << err.message();
    SendError(HTTP_ERR_NAME_NOT_RESOLVED);
  }
}

template <>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::OnResolve(
    const asio::error_code& err, tcp::resolver::iterator endpoint_iterator) {
  if (!err) {
    asio::async_connect(socket_, endpoint_iterator,
                        std::bind(&HTTPClient<nonssl_socket_t>::OnConnect, this,
                                  std::placeholders::_1));
  } else {
    FXL_VLOG(1) << "Resolve(NonSSL): " << err.message();
    SendError(HTTP_ERR_NAME_NOT_RESOLVED);
  }
}

template <typename T>
bool URLLoaderImpl::HTTPClient<T>::OnVerifyCertificate(
    bool preverified, asio::ssl::verify_context& ctx) {
  // TODO(toshik): RFC 2818 describes the steps involved in doing this for
  // HTTPS.
  char subject_name[256];
  X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
  X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);

#ifdef NETWORK_SERVICE_HTTPS_CERT_HACK
  preverified = true;
#endif
  return preverified;
}

template <>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::OnConnect(
    const asio::error_code& err) {
  if (!err) {
    socket_.async_handshake(asio::ssl::stream_base::client,
                            std::bind(&HTTPClient<ssl_socket_t>::OnHandShake,
                                      this, std::placeholders::_1));
  } else {
    FXL_VLOG(1) << "Connect(SSL): " << err.message();
    SendError(HTTP_ERR_CONNECTION_FAILED);
  }
}

template <>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::OnConnect(
    const asio::error_code& err) {
  if (!err) {
    asio::async_write(
        socket_, request_header_buf_,
        std::bind(&HTTPClient<nonssl_socket_t>::OnWriteRequestHeaders, this,
                  std::placeholders::_1, std::placeholders::_2));
  } else {
    FXL_VLOG(1) << "Connect(NonSSL): " << err.message();
    SendError(HTTP_ERR_CONNECTION_FAILED);
  }
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::OnHandShake(const asio::error_code& err) {
  if (!err) {
    asio::async_write(socket_, request_header_buf_,
                      std::bind(&HTTPClient<T>::OnWriteRequestHeaders, this,
                                std::placeholders::_1, std::placeholders::_2));
  } else {
    FXL_VLOG(1) << "HandShake: " << err.message();
    SendError(HTTP_ERR_SSL_HANDSHAKE_NOT_COMPLETED);
  }
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::OnWriteRequestHeaders(
    const asio::error_code& err, std::size_t transferred) {
  if (!err) {
    request_header_buf_.consume(transferred);

    if (request_header_buf_.size() > 0) {
      asio::async_write(
          socket_, request_header_buf_,
          std::bind(&HTTPClient<T>::OnWriteRequestHeaders, this,
                    std::placeholders::_1, std::placeholders::_2));
    } else {
      WriteRequestBody();
    }
  } else {
    FXL_VLOG(1) << "WriteRequestHeaders: " << err.message();
    // TODO(toshik): better error code?
    SendError(HTTP_ERR_FAILED);
  }
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::WriteRequestBody() {
  if (request_body_buf_.size() > 0 ||
      (request_body_reader_ &&
       request_body_reader_->ReadAvailable(&request_body_stream_))) {
    asio::async_write(socket_, request_body_buf_,
                      std::bind(&HTTPClient<T>::OnWriteRequestBody, this,
                                std::placeholders::_1, std::placeholders::_2));
  } else if (request_body_reader_ && request_body_reader_->err() != ZX_OK) {
    SendError(HTTP_ERR_FAILED);
  } else {
    // TODO(toshik): The response_ streambuf will automatically grow
    // The growth may be limited by passing a maximum size to the
    // streambuf constructor.
    asio::async_read_until(socket_, response_buf_, "\r\n",
                           std::bind(&HTTPClient<T>::OnReadStatusLine, this,
                                     std::placeholders::_1));
  }
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::OnWriteRequestBody(
    const asio::error_code& err, std::size_t transferred) {
  if (!err) {
    request_body_buf_.consume(transferred);
    WriteRequestBody();
  } else {
    FXL_VLOG(1) << "WriteRequestBody: " << err.message();
    // TODO(toshik): better error code?
    SendError(HTTP_ERR_FAILED);
  }
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::OnReadStatusLine(
    const asio::error_code& err) {
  if (!err) {
    std::istream response_stream(&response_buf_);
    response_stream >> http_version_;
    response_stream >> status_code_;
    std::string status_message;
    std::getline(response_stream, status_message_);
    if (!response_stream || http_version_.substr(0, 5) != "HTTP/") {
      FXL_VLOG(1) << "ReadStatusLine: Invalid response\n";
      SendError(HTTP_ERR_INVALID_RESPONSE);
      return;
    }
    // TODO(toshik): we don't treat any status code as an NETWORK_ERR for now

    asio::async_read_until(
        socket_, response_buf_, "\r\n\r\n",
        std::bind(&HTTPClient<T>::OnReadHeaders, this, std::placeholders::_1));
  } else {
    FXL_VLOG(1) << "ReadStatusLine: " << err.message();
  }
}

template <typename T>
zx_status_t URLLoaderImpl::HTTPClient<T>::SendStreamedBody() {
  size_t size = response_buf_.size();

  if (size > 0) {
    std::istream response_stream(&response_buf_);
    size_t done = 0;
    do {
      TransferBuffer buffer;
      size_t todo = std::min(sizeof(buffer), size - done);
      FXL_DCHECK(todo > 0);
      response_stream.read(buffer, todo);
      size_t offset = 0;
      do {
        size_t written = 0;
        zx_status_t result = response_body_stream_.write(
            0, buffer + offset, todo - offset, &written);
        if (result == ZX_ERR_SHOULD_WAIT) {
          result = response_body_stream_.wait_one(
              ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED, zx::time::infinite(),
              nullptr);
          if (result == ZX_OK)
            continue;  // retry now that the socket is ready
        }
        if (result != ZX_OK) {
          // If the other end closes the socket, ZX_ERR_PEER_CLOSED
          // can happen.
          if (result != ZX_ERR_PEER_CLOSED)
            FXL_VLOG(1) << "SendStreamedBody: result=" << result;
          return result;
        }
        offset += written;
      } while (offset < todo);
      FXL_DCHECK(offset == todo);
      done += todo;
    } while (done < size);
  }
  return ZX_OK;
}

template <typename T>
zx_status_t URLLoaderImpl::HTTPClient<T>::SendBufferedBody() {
  size_t size = response_buf_.size();

  if (size > 0) {
    // TODO(rosswang): For now, wait until we have the entire body to begin
    // writing to the VMO so that we know the size. Eventually to support larger
    // VMOs without burdening the memory unduly, perhaps we'll want to create
    // the VMO earlier and resize it as we buffer more to take advantage of
    // VMO virtualization.
    zx::vmo vmo;
    zx_status_t result = zx::vmo::create(size, 0u, &vmo);
    if (result != ZX_OK) {
      FXL_VLOG(1) << "SendBufferedBody: Unable to create vmo: " << result;
      return result;
    }

    std::istream response_stream(&response_buf_);
    size_t done = 0;
    do {
      TransferBuffer buffer;
      size_t todo = std::min(sizeof(buffer), size - done);
      FXL_DCHECK(todo > 0);
      response_stream.read(buffer, todo);
      result = vmo.write(buffer, done, todo);
      if (result != ZX_OK) {
        FXL_VLOG(1) << "SendBufferedBody: result=" << result;
        return result;
      }
      done += todo;
    } while (done < size);

    if (loader_->response_body_mode_ ==
        ::fuchsia::net::oldhttp::ResponseBodyMode::BUFFER) {
      response_.body->set_buffer(std::move(vmo));
    } else {
      FXL_DCHECK(loader_->response_body_mode_ ==
                 ::fuchsia::net::oldhttp::ResponseBodyMode::SIZED_BUFFER);
      response_.body->set_sized_buffer(
          fsl::SizedVmo(std::move(vmo), size).ToTransport());
    }
  }
  return ZX_OK;
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::ParseHeaderField(const std::string& header,
                                                    std::string* name,
                                                    std::string* value) {
  std::string::const_iterator name_end =
      std::find(header.begin(), header.end(), ':');
  *name = std::string(header.begin(), name_end);

  std::string::const_iterator value_begin =
      std::find_if(name_end + 1, header.end(), [](int c) { return c != ' '; });
  std::string::const_iterator value_end =
      std::find_if(name_end + 1, header.end(), [](int c) { return c == '\r'; });
  *value = std::string(value_begin, value_end);
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::OnReadHeaders(const asio::error_code& err) {
  if (!err) {
    std::istream response_stream(&response_buf_);
    std::string header;

    if (status_code_ == 301 || status_code_ == 302) {
      redirect_location_.clear();

      while (std::getline(response_stream, header) && header != "\r") {
        ::fuchsia::net::oldhttp::HttpHeaderPtr hdr =
            ::fuchsia::net::oldhttp::HttpHeader::New();
        std::string name, value;
        ParseHeaderField(header, &name, &value);
        if (name == "Location") {
          redirect_location_ = value;
          FXL_VLOG(1) << "Redirecting to " << redirect_location_;
        }
      }
    } else {
      ::fuchsia::net::oldhttp::URLResponse response;
      response.status_code = status_code_;
      response.status_line =
          http_version_ + " " + std::to_string(status_code_) + status_message_;
      response.url = loader_->current_url_.spec();

      while (std::getline(response_stream, header) && header != "\r") {
        ::fuchsia::net::oldhttp::HttpHeader hdr;
        std::string name, value;
        ParseHeaderField(header, &name, &value);
        hdr.name = std::move(name);
        hdr.value = std::move(value);
        response.headers.push_back(std::move(hdr));
      }

      response.body = std::make_unique<::fuchsia::net::oldhttp::URLBody>();

      switch (loader_->response_body_mode_) {
        case ::fuchsia::net::oldhttp::ResponseBodyMode::BUFFER:
        case ::fuchsia::net::oldhttp::ResponseBodyMode::SIZED_BUFFER:
          response_ = std::move(response);

          asio::async_read(socket_, response_buf_,
                           std::bind(&HTTPClient<T>::OnBufferBody, this,
                                     std::placeholders::_1));
          break;
        case ::fuchsia::net::oldhttp::ResponseBodyMode::STREAM:
        case ::fuchsia::net::oldhttp::ResponseBodyMode::BUFFER_OR_STREAM:
          zx::socket consumer;
          zx::socket producer;
          zx_status_t status = zx::socket::create(0u, &producer, &consumer);
          if (status != ZX_OK) {
            FXL_VLOG(1) << "Unable to create socket:"
                        << zx_status_get_string(status);
            return;
          }
          response_body_stream_ = std::move(producer);
          response.body->set_stream(std::move(consumer));

          loader_->SendResponse(std::move(response));

          if (SendStreamedBody() != ZX_OK) {
            response_body_stream_.reset();
            return;
          }

          asio::async_read(socket_, response_buf_, asio::transfer_at_least(1),
                           std::bind(&HTTPClient<T>::OnStreamBody, this,
                                     std::placeholders::_1));
          break;
      }
    }
  } else {
    FXL_VLOG(1) << "ReadHeaders: " << err.message();
  }
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::OnBufferBody(const asio::error_code& err) {
  // asio::error::eof happens if the other side closed their connection.
  if (err && err != asio::ssl::error::stream_truncated &&
      err != asio::error::eof) {
    // TODO: if EOF, should probably confirm we read all of the bytes (see
    // Content-Length header).
    FXL_VLOG(1) << "OnBufferBody: " << err.message() << " (" << err << ")";
    // TODO(somebody who knows asio/network errors): real translation
    SendError(HTTP_ERR_FAILED);
  } else {
    SendBufferedBody();
    loader_->SendResponse(std::move(response_));
  }
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::OnStreamBody(const asio::error_code& err) {
  if (!err && SendStreamedBody() == ZX_OK) {
    asio::async_read(
        socket_, response_buf_, asio::transfer_at_least(1),
        std::bind(&HTTPClient<T>::OnStreamBody, this, std::placeholders::_1));
  } else {
    // EOF is handled here.
    // TODO(toshik): print the error code if it is unexpected.
    // FXL_VLOG(1) << "OnStreamBody: " << err.message();
    response_body_stream_.reset();
  }
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::SendResponse(
    ::fuchsia::net::oldhttp::URLResponse response) {
  loader_->SendResponse(std::move(response));
}

template <typename T>
void URLLoaderImpl::HTTPClient<T>::SendError(int error_code) {
  loader_->SendError(error_code);
}

}  // namespace http

#if defined(ASIO_NO_EXCEPTIONS)
// If C++ exception is disabled, ASIO calls this function instead of throwing
// an exception. Do not simply return from this function. The execution should
// be aborted immediately.
//
// TODO: Abort the current thread only.
template <typename Exception>
void asio::detail::throw_exception(const Exception& e) {
  FXL_LOG(FATAL) << "Exception occurred: " << e.what();
}
#endif

#endif  // GARNET_BIN_HTTP_HTTP_NEW_CLIENT_H_
