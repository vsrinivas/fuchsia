// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_HTTP_CLIENT_H_
#define MOJO_SERVICES_NETWORK_HTTP_CLIENT_H_

#include "base/logging.h"

#include "mojo/public/cpp/system/wait.h"
#include "mojo/services/network/net_errors.h"
#include "mojo/services/network/upload_element_reader.h"

#include <asio.hpp>
#include <asio/ssl.hpp>

using asio::ip::tcp;

namespace mojo {

typedef asio::ssl::stream<tcp::socket> ssl_socket_t;
typedef tcp::socket nonssl_socket_t;

template<typename T>
class URLLoaderImpl::HTTPClient {

  static_assert(std::is_same<T, ssl_socket_t>::value ||
                std::is_same<T, nonssl_socket_t>::value,
                "requires either ssl_socket_t or nonssl_socket_t");

 public:
  static const std::set<std::string> ALLOWED_METHODS;

  static bool IsMethodAllowed(const std::string& method);

  HTTPClient<T>(URLLoaderImpl* loader,
                asio::io_service& io_service,
                asio::ssl::context& context);

  HTTPClient<T>(URLLoaderImpl* loader,
                asio::io_service& io_service);

  MojoResult CreateRequest(const std::string& server, const std::string& path,
                           const std::string& method,
                           const std::map<std::string,
                           std::string>& extra_headers,
                           const std::vector<std::unique_ptr<
                           UploadElementReader>>& element_readers);
  void Start(const std::string& server, const std::string& port);

 private:
  void OnResolve(const asio::error_code& err,
                 tcp::resolver::iterator endpoint_iterator);
  bool OnVerifyCertificate(bool preverified,
                           asio::ssl::verify_context& ctx);
  void OnConnect(const asio::error_code& err);
  void OnHandShake(const asio::error_code& err);
  void OnWriteRequest(const asio::error_code& err);
  void OnReadStatusLine(const asio::error_code& err);
  MojoResult SendBody();
  void ParseHeaderField(const std::string& header, std::string* name,
                        std::string* value);
  void OnReadHeaders(const asio::error_code& err);
  void OnReadBody(const asio::error_code& err);

  void SendResponse(URLResponsePtr response);
  void SendError(int error_code);

 public:
  unsigned int status_code_;
  std::string redirect_location_;

 private:
  URLLoaderImpl* loader_;

  tcp::resolver resolver_;
  T socket_;
  std::vector<asio::streambuf::const_buffers_type> request_bufs_;
  asio::streambuf request_header_buf_;
  asio::streambuf request_body_buf_;
  asio::streambuf response_buf_;

  std::string http_version_;
  std::string status_message_;

  ScopedDataPipeProducerHandle response_body_stream_;
};

template<typename T>
const std::set<std::string> URLLoaderImpl::HTTPClient<T>::ALLOWED_METHODS {
  "GET", "HEAD", "POST", "PUT", "DELETE", "TRACE", "CONNECT", "PATCH"
};

template<typename T>
bool URLLoaderImpl::HTTPClient<T>::IsMethodAllowed(const std::string& method) {
  return ALLOWED_METHODS.find(method) != ALLOWED_METHODS.end();
}

template<>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::
    OnResolve(const asio::error_code& err,
              tcp::resolver::iterator endpoint_iterator);
template<>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::
    OnResolve(const asio::error_code& err,
              tcp::resolver::iterator endpoint_iterator);
template<>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::
    OnConnect(const asio::error_code& err);
template<>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::
    OnConnect(const asio::error_code& err);

template<>
URLLoaderImpl::HTTPClient<ssl_socket_t>::
    HTTPClient(URLLoaderImpl* loader, asio::io_service& io_service,
           asio::ssl::context& context)
      : loader_(loader),
        resolver_(io_service),
        socket_(io_service, context) {}

template<>
URLLoaderImpl::HTTPClient<nonssl_socket_t>::
    HTTPClient(URLLoaderImpl* loader, asio::io_service& io_service)
      : loader_(loader),
        resolver_(io_service),
        socket_(io_service) {}

template<typename T>
MojoResult URLLoaderImpl::HTTPClient<T>::
    CreateRequest(const std::string& server,
                  const std::string& path,
                  const std::string& method,
                  const std::map<std::string, std::string>& extra_headers,
                  const std::vector<std::unique_ptr<
                      UploadElementReader>>& element_readers) {
  if (!IsMethodAllowed(method)) {
    LOG(ERROR) << "Method " << method << " is not allowed";
    return MOJO_RESULT_INVALID_ARGUMENT;
  }

  std::ostream request_header_stream(&request_header_buf_);

  request_header_stream << method << " " << path << " HTTP/1.1\r\n";
  request_header_stream << "Host: " << server << "\r\n";
  request_header_stream << "Accept: */*\r\n";
  // TODO(toshik): should we make this work without closing the connection?
  request_header_stream << "Connection: close\r\n";

  for (auto it = extra_headers.begin(); it != extra_headers.end(); ++it)
    request_header_stream << it->first << ": " << it->second << "\r\n";

  std::ostream request_body_stream(&request_body_buf_);

  for (auto it = element_readers.begin(); it != element_readers.end(); ++it) {
    MojoResult result = (*it)->ReadAll(&request_body_stream);
    if (result != MOJO_RESULT_OK)
      return result;
  }

  uint64_t content_length = request_body_buf_.size();
  if (content_length > 0)
    request_header_stream << "Content-Length: " << content_length << "\r\n";

  request_header_stream << "\r\n";

  request_bufs_.push_back(request_header_buf_.data());
  request_bufs_.push_back(request_body_buf_.data());

  return MOJO_RESULT_OK;
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::Start(const std::string& server,
                                         const std::string& port) {
  tcp::resolver::query query(server, port);
  resolver_.async_resolve(query,
                          std::bind(&HTTPClient<T>::OnResolve, this,
                                    std::placeholders::_1,
                                    std::placeholders::_2));
}

template<>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::
    OnResolve(const asio::error_code& err,
              tcp::resolver::iterator endpoint_iterator) {
  if (!err) {
    socket_.set_verify_mode(asio::ssl::verify_peer);
    socket_.set_verify_callback(std::bind(&HTTPClient<ssl_socket_t>::
                                          OnVerifyCertificate,
                                          this, std::placeholders::_1,
                                          std::placeholders::_2));
    asio::async_connect(socket_.lowest_layer(), endpoint_iterator,
                        std::bind(&HTTPClient<ssl_socket_t>::OnConnect, this,
                                  std::placeholders::_1));
  } else {
    LOG(ERROR) << "Resolve(SSL): " << err.message();
    SendError(net::ERR_NAME_NOT_RESOLVED);
  }
}

template<>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::
    OnResolve(const asio::error_code& err,
              tcp::resolver::iterator endpoint_iterator) {
  if (!err) {
      asio::async_connect(socket_, endpoint_iterator,
                          std::bind(&HTTPClient<nonssl_socket_t>:: OnConnect,
                                    this,
                                    std::placeholders::_1));
  } else {
    LOG(ERROR) << "Resolve(NonSSL): " << err.message();
    SendError(net::ERR_NAME_NOT_RESOLVED);
  }
}

template<typename T>
bool URLLoaderImpl::HTTPClient<T>::
    OnVerifyCertificate(bool preverified, asio::ssl::verify_context& ctx) {
  // TODO(toshik): RFC 2818 describes the steps involved in doing this for
  // HTTPS.
  char subject_name[256];
  X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
  X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
  LOG(INFO) << "Verifying " << subject_name;

#ifdef NETWORK_SERVICE_HTTPS_CERT_HACK
  preverified = true;
#endif
  return preverified;
}

template<>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::
    OnConnect(const asio::error_code& err) {
  if (!err) {
    socket_.async_handshake(asio::ssl::stream_base::client,
                            std::bind(&HTTPClient<ssl_socket_t>::OnHandShake,
                                      this,
                                      std::placeholders::_1));
  } else {
    LOG(ERROR) << "Connect(SSL): " << err.message();
    SendError(net::ERR_CONNECTION_FAILED);
  }
}

template<>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::
    OnConnect(const asio::error_code& err) {
  if (!err) {
    asio::async_write(socket_, request_bufs_,
                      std::bind(&HTTPClient<nonssl_socket_t>::OnWriteRequest,
                                this,
                                std::placeholders::_1));
  } else {
    LOG(ERROR) << "Connect(NonSSL): " << err.message();
    SendError(net::ERR_CONNECTION_FAILED);
  }
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::OnHandShake(const asio::error_code& err) {
  if (!err) {
    asio::async_write(socket_, request_bufs_,
                      std::bind(&HTTPClient<T>::OnWriteRequest, this,
                                std::placeholders::_1));
  } else {
    LOG(ERROR) << "HandShake: " << err.message();
    SendError(net::ERR_SSL_HANDSHAKE_NOT_COMPLETED);
  }
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::OnWriteRequest(const asio::error_code& err) {
  if (!err) {
    // TODO(toshik): The response_ streambuf will automatically grow
    // The growth may be limited by passing a maximum size to the
    // streambuf constructor.
    asio::async_read_until(socket_, response_buf_, "\r\n",
                           std::bind(&HTTPClient<T>::OnReadStatusLine, this,
                                     std::placeholders::_1));
  } else {
    LOG(ERROR) << "WriteRequest: " << err.message();
    // TODO(toshik): better error code?
    SendError(net::ERR_FAILED);
  }
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::
    OnReadStatusLine(const asio::error_code& err) {
  if (!err) {
    std::istream response_stream(&response_buf_);
    response_stream >> http_version_;
    response_stream >> status_code_;
    std::string status_message;
    std::getline(response_stream, status_message_);
    if (!response_stream || http_version_.substr(0, 5) != "HTTP/") {
      LOG(ERROR) << "ReadStatusLine: Invalid response\n";
      SendError(net::ERR_INVALID_RESPONSE);
      return;
    }
    if (!(status_code_ >= 200 && status_code_ <= 299) &&
        status_code_ != 301 && status_code_ != 302) {
      // TODO(toshik): handle more status codes
      LOG(ERROR) << "ReadStatusLine: Status code " << status_code_;
      SendError(net::ERR_NOT_IMPLEMENTED);
      return;
    }

    asio::async_read_until(socket_, response_buf_, "\r\n\r\n",
                           std::bind(&HTTPClient<T>::OnReadHeaders, this,
                                     std::placeholders::_1));
  } else {
    LOG(ERROR) << "ReadStatusLine: " << err;
  }
}

template<typename T>
MojoResult URLLoaderImpl::HTTPClient<T>::SendBody() {
  uint32_t size = response_buf_.size();

  if (size > 0) {
    std::istream response_stream(&response_buf_);
    uint32_t done = 0;
    do {
      uint32_t todo = size - done;
      void *buf;
      uint32_t num_bytes;

      MojoResult result = BeginWriteDataRaw(response_body_stream_.get(),
                                            &buf, &num_bytes,
                                            MOJO_WRITE_DATA_FLAG_NONE);
      if (result == MOJO_RESULT_SHOULD_WAIT) {
        result = Wait(response_body_stream_.get(),
                      MOJO_HANDLE_SIGNAL_WRITABLE,
                      MOJO_DEADLINE_INDEFINITE,
                      nullptr);
        if (result == MOJO_RESULT_OK)
          continue; // retry now that the data pipe is ready
      }
      if (result != MOJO_RESULT_OK) {
        // If the other end closes the data pipe,
        // MOJO_RESULT_FAILED_PRECONDITION can happen.
        if (result != MOJO_RESULT_FAILED_PRECONDITION)
          LOG(ERROR) << "SendBody: result=" << result;
        return result;
      }

      if (num_bytes < todo)
        todo = num_bytes;

      if (todo)
        response_stream.read((char*)buf, todo);

      EndWriteDataRaw(response_body_stream_.get(), todo);
      done += todo;
    } while (done < size);
  }
  return MOJO_RESULT_OK;
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::ParseHeaderField(const std::string& header,
                                                    std::string* name,
                                                    std::string* value) {
  std::string::const_iterator name_end = std::find(header.begin(), header.end(),
                                                   ':');
  *name = std::string(header.begin(), name_end);

  std::string::const_iterator value_begin =
    std::find_if(name_end + 1, header.end(), [](int c) { return c != ' '; });
  std::string::const_iterator value_end =
    std::find_if(name_end + 1, header.end(), [](int c) { return c == '\r'; });
  *value = std::string(value_begin, value_end);
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::OnReadHeaders(const asio::error_code& err) {
  if (!err) {
    std::istream response_stream(&response_buf_);
    std::string header;

    if (status_code_ == 301 || status_code_ == 302) {
      redirect_location_.clear();

      while (std::getline(response_stream, header) && header != "\r") {
        HttpHeaderPtr hdr = HttpHeader::New();
        std::string name, value;
        ParseHeaderField(header, &name, &value);
        if (name == "Location") {
          redirect_location_ = value;
          LOG(INFO) << "Redirecting to " << redirect_location_;
        }
      }
    } else {
      URLResponsePtr response = URLResponse::New();

      response->status_code = status_code_;
      response->status_line =
        http_version_ + " " + std::to_string(status_code_) + status_message_;

      while (std::getline(response_stream, header) && header != "\r") {
        HttpHeaderPtr hdr = HttpHeader::New();
        std::string name, value;
        ParseHeaderField(header, &name, &value);
        hdr->name = name;
        hdr->value = value;
        response->headers.push_back(std::move(hdr));
      }

      DataPipe data_pipe;
      response_body_stream_ = std::move(data_pipe.producer_handle);
      response->body = std::move(data_pipe.consumer_handle);

      loader_->SendResponse(std::move(response));

      if (SendBody() != MOJO_RESULT_OK) {
        response_body_stream_.reset();
        return;
      }

      asio::async_read(socket_, response_buf_,
                       asio::transfer_at_least(1),
                       std::bind(&HTTPClient<T>::OnReadBody, this,
                                 std::placeholders::_1));
    }
  } else {
    LOG(ERROR) << "ReadHeaders: " << err;
  }
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::OnReadBody(const asio::error_code& err) {
  if (!err && SendBody() == MOJO_RESULT_OK) {
    asio::async_read(socket_, response_buf_,
                     asio::transfer_at_least(1),
                     std::bind(&HTTPClient<T>::OnReadBody, this,
                               std::placeholders::_1));
  } else {
    // EOF is handled here.
    // TODO(toshik): print the error code if it is unexpected.
    // LOG(INFO) << "OnReadBody: " << err.message();
    response_body_stream_.reset();
  }
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::SendResponse(URLResponsePtr response) {
  loader_->SendResponse(std::move(response));
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::SendError(int error_code) {
  loader_->SendError(error_code);
}

} // namespace mojo

#if defined(ASIO_NO_EXCEPTIONS)
// ASIO doesn't provide this if exception is not enabled
template <typename Exception>
void asio::detail::throw_exception(const Exception& e) {
  LOG(ERROR) << "Exception occurred: " << e.what();
}
#endif

#endif /* MOJO_SERVICES_NETWORK_HTTP_CLIENT_H_ */
