// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_CURL_H_
#define SRC_DEVELOPER_DEBUG_SHARED_CURL_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <curl/curl.h>

#include "lib/fit/function.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace debug {

// To use Curl, one must add something like the following to the beginning of their main() function
// (and include necessary header files).
//     Curl::GlobalInit();
//     auto deferred_cleanup = fit::defer([]{ Curl::GlobalCleanup(); });
// This is due to the thread-unsafety of curl_global_init() and curl_global_cleanup(), see
// https://curl.se/libcurl/c/curl_global_init.html and
// https://curl.se/libcurl/c/curl_global_cleanup.html.
//
// Curl must be constructed through fxl::MakeRefCounted<Curl>().
//
// Example usage:
//     int main() {
//       Curl::GlobalInit();
//       auto deferred_cleanup = fit::defer([]{ Curl::GlobalCleanup(); });
//
//       // do something else and maybe spawn some threads
//
//       auto curl = fxl::MakeRefCounted<Curl>();
//       // curl->......
//
//     }
class Curl : public fxl::RefCountedThreadSafe<Curl> {
 public:
  class Error {
   public:
    explicit Error(CURLcode code) : code_(code) {}

    Error& operator=(CURLcode code) {
      code_ = code;
      return *this;
    }

    bool operator==(const Error& other) const { return other.code_ == code_; }

    explicit operator CURLcode() const { return code_; }
    explicit operator bool() const { return code_ != CURLE_OK; }

    std::string ToString() const { return curl_easy_strerror(code_); }

   private:
    CURLcode code_;
  };

  // Escapes URL strings (converts all letters consider illegal in URLs to their %XX versions)
  static std::string Escape(const std::string& input);

  // Must be called before any threads are spawned and creation of Curl object.
  static void GlobalInit();
  // Need to be called after all threads are joined for resource cleanup.
  static void GlobalCleanup();

  // Callback when we receive data from libcurl. The return value should be the number of bytes
  // successfully processed (i.e. if we are passing this data to the write() syscall and it returns
  // a short bytes written count, we should as well).
  using DataCallback = fit::function<size_t(const std::string&)>;

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

  // Run the curl request synchronously.
  Error Perform();

  // Run the curl request asynchronously. Invoke the callback when done.
  void Perform(fit::callback<void(Curl*, Error)> cb);

  // Get the response code from the request. Undefined if the request hasn't
  // run.
  long ResponseCode();

 private:
  class MultiHandle;

  Curl();
  ~Curl();
  void FreeSList();
  void PrepareToPerform();

  CURL* curl_ = nullptr;
  struct curl_slist* slist_ = nullptr;
  bool get_body_ = true;

  std::string post_data_;
  std::vector<std::string> headers_;
  fit::callback<void(Curl*, Error)> multi_cb_;
  DataCallback header_callback_ = [](const std::string& data) { return data.size(); };
  DataCallback data_callback_ = [](const std::string& data) { return data.size(); };

  FRIEND_REF_COUNTED_THREAD_SAFE(Curl);
  FRIEND_MAKE_REF_COUNTED(Curl);
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_CURL_H_
