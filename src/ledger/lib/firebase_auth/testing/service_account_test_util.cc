// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fsl/vmo/strings.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/network_wrapper/fake_network_wrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "src/lib/files/file.h"

namespace service_account {

namespace http = ::fuchsia::net::oldhttp;

std::string GetSuccessResponseBodyForTest(std::string token,
                                          size_t expiration) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();

  writer.Key("idToken");
  writer.String(token);

  writer.Key("expiresIn");
  writer.String(fxl::NumberToString(expiration));

  writer.EndObject();

  return std::string(string_buffer.GetString(), string_buffer.GetSize());
}

http::URLResponse GetResponseForTest(http::HttpErrorPtr error, uint32_t status,
                                     std::string body) {
  http::URLResponse response;
  response.error = std::move(error);
  response.status_code = status;
  fsl::SizedVmo buffer;
  if (!fsl::VmoFromString(body, &buffer)) {
    ADD_FAILURE() << "Unable to convert string to Vmo.";
  }
  response.body = http::URLBody::New();
  response.body->set_buffer(std::move(buffer).ToTransport());
  return response;
}

}  // namespace service_account
