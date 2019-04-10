// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/curl.h"

namespace zxdb {

size_t Curl::global_init = 0;

size_t DoHeaderCallback(char* data, size_t size, size_t nitems, void* curl) {
  Curl* self = reinterpret_cast<Curl*>(curl);
  return self->header_callback_(std::string(data, size * nitems));
}

size_t DoDataCallback(char* data, size_t size, size_t nitems, void* curl) {
  Curl* self = reinterpret_cast<Curl*>(curl);
  return self->data_callback_(std::string(data, size * nitems));
}

std::unique_ptr<Curl> Curl::Create() {
  if (!global_init && !curl_global_init(CURL_GLOBAL_SSL)) {
    global_init++;
  }

  if (!global_init) {
    return nullptr;
  }

  auto curl = std::unique_ptr<Curl>(new Curl(curl_easy_init()));
  if (!curl->curl_) {
    return nullptr;
  }

  return curl;
}

Curl::~Curl() {
  if (curl_) {
    curl_easy_cleanup(curl_);

    if (!--global_init) {
      curl_global_cleanup();
    }
  }
}

void Curl::set_post_data(const std::map<std::string, std::string>& items) {
  std::string encoded;

  for (const auto& item : items) {
    if (!encoded.empty()) {
      encoded += "&";
    }

    encoded += Escape(item.first);
    encoded += "=";
    encoded += Escape(item.second);
  }

  set_post_data(std::move(encoded));
}

std::string Curl::Escape(const std::string& input) {
  auto escaped = curl_easy_escape(curl_, input.c_str(), input.size());
  std::string ret(escaped);
  curl_free(escaped);
  return ret;
}

Curl::Error Curl::Perform() {
  curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, DoHeaderCallback);
  curl_easy_setopt(curl_, CURLOPT_HEADERDATA, this);
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, DoDataCallback);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, this);

  if (post_data_.empty()) {
    curl_easy_setopt(curl_, CURLOPT_POST, 0);
  } else {
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, post_data_.data());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, post_data_.size());
  }

  struct curl_slist* list = nullptr;
  for (const auto& header : headers_) {
    list = curl_slist_append(list, header.c_str());
  }

  curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, list);
  auto ret = curl_easy_perform(curl_);
  curl_slist_free_all(list);

  return Error(ret);
}

long Curl::ResponseCode() {
  long ret;

  // The function itself should always return CURLE_OK for this set of
  // arguments. I don't want to grow a dependency on a utility library if the
  // only thing I'm taking is a debug check macro, but if I had one I'd use it
  // here.
  curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &ret);
  return ret;
}

}  // namespace curl
