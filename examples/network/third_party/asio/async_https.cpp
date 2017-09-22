//
// async_client.cpp
// ~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2015 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <asio.hpp>
#include "asio/ssl.hpp"

using asio::ip::tcp;

class client
{
public:
  client(asio::io_service& io_service,
	 asio::ssl::context& context,
	 const std::string& server, const std::string& port,
	 const std::string& path)
    : resolver_(io_service),
      socket_(io_service, context)
  {

    // Form the request. We specify the "Connection: close" header so that the
    // server will close the socket after transmitting the response. This will
    // allow us to treat all data up until the EOF as the content.
    std::ostream request_stream(&request_);
    request_stream << "GET " << path << " HTTP/1.0\r\n";
    request_stream << "Host: " << server << "\r\n";
    request_stream << "Accept: */*\r\n";
    request_stream << "Connection: close\r\n\r\n";

    tcp::resolver::query query(server, port);
    resolver_.async_resolve(query,
			    std::bind(&client::handle_resolve, this,
				      std::placeholders::_1,
				      std::placeholders::_2));
  }

private:
  void handle_resolve(const asio::error_code& err,
		      tcp::resolver::iterator endpoint_iterator)
  {
    if (!err)
    {
      socket_.set_verify_mode(asio::ssl::verify_peer);
      socket_.set_verify_callback(
	  std::bind(&client::verify_certificate, this, std::placeholders::_1,
		    std::placeholders::_2));

      asio::async_connect(socket_.lowest_layer(), endpoint_iterator,
			std::bind(&client::handle_connect, this,
				  std::placeholders::_1));
    }
    else
    {
      std::cout << "Error: " << err.message() << "\n";
    }
  }

  bool verify_certificate(bool preverified,
      asio::ssl::verify_context& ctx)
  {
    // The verify callback can be used to check whether the certificate that is
    // being presented is valid for the peer. For example, RFC 2818 describes
    // the steps involved in doing this for HTTPS. Consult the OpenSSL
    // documentation for more details. Note that the callback is called once
    // for each certificate in the certificate chain, starting from the root
    // certificate authority.

    // In this example we will simply print the certificate's subject name.
    char subject_name[256];
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
    // std::cout << "Verifying " << subject_name << "\n";

    // TODO: load the certificate and remove the next line
    preverified = true;

    return preverified;
  }

  void handle_connect(const asio::error_code& err)
  {
    if (!err)
    {
      // The connection was successful. Send the request.
      socket_.async_handshake(asio::ssl::stream_base::client,
			      std::bind(&client::handle_handshake, this,
					std::placeholders::_1));
    }
    else
    {
      std::cout << "Error: " << err.message() << "\n";
    }
  }

  void handle_handshake(const asio::error_code& err)
  {
    if (!err)
    {
      // The connection was successful. Send the request.
      asio::async_write(socket_, request_,
          std::bind(&client::handle_write_request, this,
            std::placeholders::_1));
    }
    else
    {
      std::cout << "Error: " << err.message() << "\n";
    }
  }

  void handle_write_request(const asio::error_code& err)
  {
    if (!err)
    {
      // Read the response status line. The response_ streambuf will
      // automatically grow to accommodate the entire line. The growth may be
      // limited by passing a maximum size to the streambuf constructor.
      asio::async_read_until(socket_, response_, "\r\n",
          std::bind(&client::handle_read_status_line, this,
            std::placeholders::_1));
    }
    else
    {
      std::cout << "Error: " << err.message() << "\n";
    }
  }

  void handle_read_status_line(const asio::error_code& err)
  {
    if (!err)
    {
      // Check that response is OK.
      std::istream response_stream(&response_);
      std::string http_version;
      response_stream >> http_version;
      unsigned int status_code;
      response_stream >> status_code;
      std::string status_message;
      std::getline(response_stream, status_message);
      if (!response_stream || http_version.substr(0, 5) != "HTTP/")
      {
        std::cout << "Invalid response\n";
        return;
      }
      if (status_code != 200)
      {
        std::cout << "Response returned with status code ";
        std::cout << status_code << "\n";
        return;
      }

      // Read the response headers, which are terminated by a blank line.
      asio::async_read_until(socket_, response_, "\r\n\r\n",
          std::bind(&client::handle_read_headers, this,
            std::placeholders::_1));
    }
    else
    {
      std::cout << "Error: " << err << "\n";
    }
  }

  void handle_read_headers(const asio::error_code& err)
  {
    if (!err)
    {
      // Process the response headers.
      std::istream response_stream(&response_);
      std::string header;
      while (std::getline(response_stream, header) && header != "\r")
        std::cout << header << "\n";
      std::cout << "\n";

      // Write whatever content we already have to output.
      if (response_.size() > 0)
        std::cout << &response_;

      // Start reading remaining data until EOF.
      asio::async_read(socket_, response_,
          asio::transfer_at_least(1),
          std::bind(&client::handle_read_content, this,
            std::placeholders::_1));
    }
    else
    {
      std::cout << "Error: " << err << "\n";
    }
  }

  void handle_read_content(const asio::error_code& err)
  {
    if (!err)
    {
      // Write all of the data that has been read so far.
      std::cout << &response_;

      // Continue reading remaining data until EOF.
      asio::async_read(socket_, response_,
          asio::transfer_at_least(1),
          std::bind(&client::handle_read_content, this,
            std::placeholders::_1));
    }
    else if (err != asio::error::eof)
    {
      std::cout << "Error: " << err << "\n";
    }
  }

  tcp::resolver resolver_;
  asio::ssl::stream<asio::ip::tcp::socket> socket_;
  asio::streambuf request_;
  asio::streambuf response_;
};

int main(int argc, char* argv[])
{
  {
    if (argc != 4)
    {
      std::cout << "Usage: async_https <server> <port> <path>\n";
      std::cout << "Example:\n";
      std::cout << "  async_https www.boost.org 433 /LICENSE_1_0.txt\n";
      return 1;
    }

    asio::ssl::context ctx(asio::ssl::context::sslv23);
    //ctx.load_verify_file("ca.pem");
    ctx.set_default_verify_paths();

    asio::io_service io_service;
    client c(io_service, ctx, argv[1], argv[2], argv[3]);
    io_service.run();
  }
  return 0;
}

#if defined(ASIO_NO_EXCEPTIONS)
// ASIO doesn't provide this if exception is not enabled
template <typename Exception>
void asio::detail::throw_exception(const Exception& e) {
  std::cerr << "Exception occurred: " << e.what() << std::endl;
  assert(0);
}
#endif
