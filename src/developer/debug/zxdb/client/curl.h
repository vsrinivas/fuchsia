// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CURL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CURL_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <curl/curl.h>

#include "lib/fit/function.h"

namespace zxdb {

namespace {
class CurlFDWatcher;
}  // namespace

class Curl {
 public:
  class Error {
   public:
    explicit Error(CURLcode code) : code_(code) {}

    Error& operator=(CURLcode code) {
      code_ = code;
      return *this;
    }

    bool operator==(const Error& other) { return other.code_ == code_; }

    operator CURLcode() { return code_; }
    operator bool() { return code_ != CURLE_OK; }

    std::string ToString() { return curl_easy_strerror(code_); }

   private:
    CURLcode code_;
  };

  // Callback when we receive data from libcurl. The return value should be the number of bytes
  // successfully processed (i.e. if we are passing this data to the write() syscall and it returns
  // a short bytes written count, we should as well).
  using DataCallback = fit::function<size_t(const std::string&)>;

  Curl();
  ~Curl();
  Curl(Curl&) = delete;
  Curl(Curl&& other) = delete;

  static std::shared_ptr<Curl> MakeShared();

  Error SetURL(const std::string& url) {
    return Error(curl_easy_setopt(curl_, CURLOPT_URL, url.c_str()));
  }

  const std::string& post_data() { return post_data_; }
  void set_post_data(const std::string& data) { post_data_ = data; }
  void set_post_data(const std::map<std::string, std::string>& items);

  std::vector<std::string>& headers() { return headers_; }
  bool& get_body() { return get_body_; }

  void set_data_callback(DataCallback handler) { data_callback_ = std::move(handler); }
  void set_header_callback(DataCallback handler) { header_callback_ = std::move(handler); }

  // Believe it or not this takes a curl handle, so it can't be static.
  std::string Escape(const std::string& input);

  // Run the curl request synchronously.
  Error Perform();

  // Run the curl request asynchronously. Invoke the callback when done.
  void Perform(fit::callback<void(Curl*, Error)> cb);

  // Get the response code from the request. Undefined if the request hasn't
  // run.
  long ResponseCode();

 private:
  friend class CurlFDWatcher;

  friend size_t DoHeaderCallback(char* data, size_t size, size_t nitems, void* curl);
  friend size_t DoDataCallback(char* data, size_t size, size_t nitems, void* curl);

  static CURLM* multi_handle;

  static void InitMulti();

  void FreeSList();
  void PrepareToPerform();

  // Effectively a count of the number of Curl objects outstanding. When it first becomes nonzero we
  // run curl_global_init() and when it becomes zero again we run curl_global_cleanup().
  static size_t global_init;

  CURL* curl_ = nullptr;
  struct curl_slist* slist_ = nullptr;
  bool get_body_ = true;

  std::string post_data_;
  std::weak_ptr<Curl> weak_self_ref_;
  std::shared_ptr<Curl> self_ref_;
  std::vector<std::string> headers_;
  fit::callback<void(Curl*, Error)> multi_cb_;
  DataCallback header_callback_ = [](const std::string& data) { return data.size(); };
  DataCallback data_callback_ = [](const std::string& data) { return data.size(); };
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CURL_H_
