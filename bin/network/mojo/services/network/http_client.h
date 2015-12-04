// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_HTTP_CLIENT_H_
#define MOJO_SERVICES_NETWORK_HTTP_CLIENT_H_

#include <iostream>
#include "mojo/services/network/network_error.h"

#include <asio.hpp>
#include <asio/ssl.hpp>

using asio::ip::tcp;

namespace mojo {

typedef asio::ssl::stream<tcp::socket> ssl_socket_t;
typedef tcp::socket nonssl_socket_t;

template<typename T>
class URLLoaderImpl::HTTPClient {

  static_assert(std::is_same<T, ssl_socket_t>::value || std::is_same<T, nonssl_socket_t>::value, "requires either ssl_socket_t or nonssl_socket_t");

 public:
  HTTPClient<T>(URLLoaderImpl* loader,
             asio::io_service& io_service,
             asio::ssl::context& context,
             const std::string& server, const std::string& port,
             const std::string& path);

  HTTPClient<T>(URLLoaderImpl* loader,
            asio::io_service& io_service,
            const std::string& server, const std::string& port,
            const std::string& path);
 private:
  void CreateRequest(const std::string& server, const std::string& path);
  void OnResolve(const asio::error_code& err,
                 tcp::resolver::iterator endpoint_iterator);
  bool OnVerifyCertificate(bool preverified,
                           asio::ssl::verify_context& ctx);
  void OnConnect(const asio::error_code& err);
  void OnHandShake(const asio::error_code& err);
  void OnWriteRequest(const asio::error_code& err);
  void OnReadStatusLine(const asio::error_code& err);
  MojoResult SendBody();
  void ParseHeaderField(const std::string& header, std::string& name, std::string& value);
  void OnReadHeaders(const asio::error_code& err);
  void OnReadBody(const asio::error_code& err);

 public:
  unsigned int status_code_;
        std::string redirect_location_;

 private:
  URLLoaderImpl* loader_;

  tcp::resolver resolver_;
  T socket_;
  asio::streambuf request_buf_;
  asio::streambuf response_buf_;

  std::string http_version_;
  std::string status_message_;

  ScopedDataPipeProducerHandle response_body_stream_;
};

template<>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::OnResolve(const asio::error_code& err,
                              tcp::resolver::iterator endpoint_iterator);
template<>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::OnResolve(const asio::error_code& err,
                              tcp::resolver::iterator endpoint_iterator);
template<>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::OnConnect(const asio::error_code& err);
template<>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::OnConnect(const asio::error_code& err);


template<>
URLLoaderImpl::HTTPClient<ssl_socket_t>::HTTPClient(URLLoaderImpl* loader,
                        asio::io_service& io_service,
                        asio::ssl::context& context,
                        const std::string& server, const std::string& port,
                        const std::string& path)
  : loader_(loader),
    resolver_(io_service),
    socket_(io_service, context) {
  CreateRequest(server, path);

  tcp::resolver::query query(server, port);
  resolver_.async_resolve(query,
                          std::bind(&HTTPClient<ssl_socket_t>::OnResolve, this,
                                    std::placeholders::_1,
                                    std::placeholders::_2));
}

template<>
URLLoaderImpl::HTTPClient<nonssl_socket_t>::HTTPClient(URLLoaderImpl* loader,
                         asio::io_service& io_service,
                         const std::string& server, const std::string& port,
                         const std::string& path)
  : loader_(loader),
    resolver_(io_service),
    socket_(io_service) {
  CreateRequest(server, path);

  tcp::resolver::query query(server, port);
  resolver_.async_resolve(query,
                          std::bind(&HTTPClient<nonssl_socket_t>::OnResolve, this,
                                    std::placeholders::_1,
                                    std::placeholders::_2));
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::CreateRequest(const std::string& server,
                                                 const std::string& path)
{
  // We specify the "Connection: close" header so that the server will
  // close the socket after transmitting the response. This will allow us
  // to treat all data up until the EOF as the content.
  std::ostream request_stream(&request_buf_);
  request_stream << "GET " << path << " HTTP/1.0\r\n";
  request_stream << "Host: " << server << "\r\n";
  request_stream << "Accept: */*\r\n";
  request_stream << "Connection: close\r\n\r\n";
}

template<>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::OnResolve(const asio::error_code& err,
                              tcp::resolver::iterator endpoint_iterator)
{
  if (!err) {
    socket_.set_verify_mode(asio::ssl::verify_peer);
    socket_.set_verify_callback(std::bind(&HTTPClient<ssl_socket_t>::OnVerifyCertificate,
                                          this, std::placeholders::_1,
                                          std::placeholders::_2));
    asio::async_connect(socket_.lowest_layer(), endpoint_iterator,
                        std::bind(&HTTPClient<ssl_socket_t>::OnConnect, this,
                                  std::placeholders::_1));
  } else {
    std::cout << "Error: Resolve(SSL): " << err.message() << "\n";
  }
}

template<>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::OnResolve(
                                              const asio::error_code& err,
                              tcp::resolver::iterator endpoint_iterator)
{
  if (!err) {
      asio::async_connect(socket_, endpoint_iterator,
                          std::bind(&HTTPClient<nonssl_socket_t>::OnConnect, this,
                                    std::placeholders::_1));
  } else {
    std::cout << "Error: Resolve(NonSSL): " << err.message() << "\n";
  }
}

template<typename T>
bool URLLoaderImpl::HTTPClient<T>::OnVerifyCertificate(bool preverified,
                                                   asio::ssl::verify_context& ctx)
{
  // TODO(toshik): RFC 2818 describes the steps involved in doing this for
  // HTTPS.
  char subject_name[256];
  X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
  X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
  std::cout << "Verifying " << subject_name << "\n";

#ifdef NETWORK_SERVICE_HTTPS_CERT_HACK
  preverified = true;
#endif
  return preverified;
}

template<>
void URLLoaderImpl::HTTPClient<ssl_socket_t>::OnConnect(const asio::error_code& err)
{
  if (!err) {
    socket_.async_handshake(asio::ssl::stream_base::client,
                            std::bind(&HTTPClient<ssl_socket_t>::OnHandShake, this,
                                      std::placeholders::_1));
  } else {
    std::cout << "Error: Connect(SSL): " << err.message() << "\n";
  }
}

template<>
void URLLoaderImpl::HTTPClient<nonssl_socket_t>::OnConnect(const asio::error_code& err)
{
  if (!err) {
    asio::async_write(socket_, request_buf_,
                      std::bind(&HTTPClient<nonssl_socket_t>::OnWriteRequest, this,
                                std::placeholders::_1));
  } else {
    std::cout << "Error: Connect(NonSSL): " << err.message() << "\n";
  }
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::OnHandShake(const asio::error_code& err)
{
  if (!err) {
    asio::async_write(socket_, request_buf_,
                      std::bind(&HTTPClient<T>::OnWriteRequest, this,
                                std::placeholders::_1));
  } else {
    std::cout << "Error: HandShake: " << err.message() << "\n";
  }
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::OnWriteRequest(const asio::error_code& err)
{
  if (!err) {
    // TODO(toshik): The response_ streambuf will automatically grow
    // The growth may be limited by passing a maximum size to the
    // streambuf constructor.
    asio::async_read_until(socket_, response_buf_, "\r\n",
                           std::bind(&HTTPClient<T>::OnReadStatusLine, this,
                                     std::placeholders::_1));
  } else {
    std::cout << "Error: WriteRequest: " << err.message() << "\n";
  }
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::OnReadStatusLine(const asio::error_code& err)
{
  if (!err) {
    std::istream response_stream(&response_buf_);
    response_stream >> http_version_;
    response_stream >> status_code_;
    std::string status_message;
    std::getline(response_stream, status_message_);
    if (!response_stream || http_version_.substr(0, 5) != "HTTP/") {
      std::cout << "Error: ReadStatusLine: Invalid response\n";
      return;
    }
    if (!(status_code_ >= 200 && status_code_ <= 299) &&
        status_code_ != 301 && status_code_ != 302) {
      // TODO(toshik): handle more status codes
      std::cout << "Error: ReadStatusLine: Status code ";
      std::cout << status_code_ << "\n";
      return;
    }

    asio::async_read_until(socket_, response_buf_, "\r\n\r\n",
                           std::bind(&HTTPClient<T>::OnReadHeaders, this,
                                     std::placeholders::_1));
  } else {
    std::cout << "Error: ReadStatusLine: " << err << "\n";
  }
}

template<typename T>
MojoResult URLLoaderImpl::HTTPClient<T>::SendBody()
{
  if (response_buf_.size() > 0) {
    uint32_t size = response_buf_.size();
    uint32_t done = 0;
    std::istream response_stream(&response_buf_);
    while (done < size) {
      uint32_t todo = size - done;
      void *buf;
      uint32_t num_bytes;
      MojoResult result = BeginWriteDataRaw(response_body_stream_.get(),
                                            &buf, &num_bytes,
                                            MOJO_WRITE_DATA_FLAG_NONE);
      if (result == MOJO_RESULT_SHOULD_WAIT) {
        // TODO(toshik): we should handle this condition with AsyncWaiter
        usleep(1000);
        continue;
      }
      if (result != MOJO_RESULT_OK) {
        std::cout << "Warning: SendBody: BeginWriteDataRAW: result="
                  << result << std::endl;
        // TODO(toshik): how to handle this?
        response_body_stream_.reset();
        response_buf_.consume(size);
        return result;
      }

      if (num_bytes < todo)
        todo = num_bytes;

      if (todo) {
        response_stream.read((char*)buf, todo);
      }
      EndWriteDataRaw(response_body_stream_.get(), todo);
      done += todo;

      if (done < size) {
        // TODO(johngro): I am a very evil man.
        //
        // Why this exists and what we should do about it:
        //
        // Ideally, we'd like to register a callback that means "tell me when
        // I can write N bytes to the data pipe," but Mojo doesn't support that
        // behavior today.  As such, we have two options - we can just call
        // BeginWriteDataRaw and it can tell us how many bytes it actually
        // wrote, or we can register an AsyncWaiter for Writable.  Both methods
        // respond when _any_ amount of bytes can be written.
        //
        // In the very short term (this week), we need to hack this to work,
        // hence the blocking sleep behavior.  In the less-short term, we'll
        // move this to an AsyncWaiter so we can participate in the main message
        // loop and avoid blocking _everything_.  In the longer term, we want a
        // callback that's aware of how many bytes we can write.
        usleep(1000);
      }
    }
    response_buf_.consume(size);
  }

  return MOJO_RESULT_OK;
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::ParseHeaderField(const std::string& header,
                                                       std::string& name,
                                                       std::string& value)
{
  std::string::const_iterator name_end = std::find(header.begin(),
                                                      header.end(), ':');
  name = std::string(header.begin(), name_end);

  std::string::const_iterator value_begin =
    std::find_if(name_end + 1, header.end(), [](int c) { return c != ' '; });
  std::string::const_iterator value_end =
    std::find_if(name_end + 1, header.end(), [](int c) { return c == '\r'; });
  value = std::string(value_begin, value_end);
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::OnReadHeaders(const asio::error_code& err)
{
  if (!err) {
    std::istream response_stream(&response_buf_);
    std::string header;

    if (status_code_ == 301 || status_code_ == 302) {
      redirect_location_.clear();

      while (std::getline(response_stream, header) && header != "\r") {
        HttpHeaderPtr hdr = HttpHeader::New();
        std::string name, value;
        ParseHeaderField(header, name, value);
        if (name == "Location") {
          redirect_location_ = value;
          std::cout << "Redirecting to " << redirect_location_ << "\n";
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
        ParseHeaderField(header, name, value);
        hdr->name = name;
        hdr->value = value;
        response->headers.push_back(hdr.Pass());
      }

      DataPipe data_pipe;
      response_body_stream_ = data_pipe.producer_handle.Pass();
      response->body = data_pipe.consumer_handle.Pass();

      loader_->SendResponse(response.Pass());

      if (SendBody() != MOJO_RESULT_OK)
        return;

      asio::async_read(socket_, response_buf_,
                       asio::transfer_at_least(1),
                       std::bind(&HTTPClient<T>::OnReadBody, this,
                                 std::placeholders::_1));
    }
  } else {
    std::cout << "Error: ReadHeaders: " << err << "\n";
  }
}

template<typename T>
void URLLoaderImpl::HTTPClient<T>::OnReadBody(const asio::error_code& err)
{
  if (!err) {
    if (SendBody() != MOJO_RESULT_OK)
      return;

    asio::async_read(socket_, response_buf_,
                     asio::transfer_at_least(1),
                     std::bind(&HTTPClient<T>::OnReadBody, this,
                               std::placeholders::_1));
  } else {
    // std::cout << "Error: " << err << std::endl;
    // TODO(toshi): handle EOF error
    response_body_stream_.reset();
  }
}

} // namespace mojo

#if defined(ASIO_NO_EXCEPTIONS)
// ASIO doesn't provide this if exception is not enabled
template <typename Exception>
void asio::detail::throw_exception(const Exception& e)
{
}
#endif

#endif /* MOJO_SERVICES_NETWORK_HTTP_CLIENT_H_ */
