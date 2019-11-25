// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/curl.h"

#include "src/developer/debug/shared/fd_watcher.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {
namespace {

class CurlFDWatcher : public debug_ipc::FDWatcher {
 public:
  static CurlFDWatcher instance;
  static std::map<int, debug_ipc::MessageLoop::WatchHandle> watches;
  static bool cleanup_pending;

  void OnFDReady(int fd, bool read, bool write, bool err) override;

 private:
  CurlFDWatcher() = default;
};

bool CurlFDWatcher::cleanup_pending = false;
CurlFDWatcher CurlFDWatcher::instance;
std::map<int, debug_ipc::MessageLoop::WatchHandle> CurlFDWatcher::watches;

void CurlFDWatcher::OnFDReady(int fd, bool read, bool write, bool err) {
  int _ignore;
  int action = 0;

  if (read)
    action |= CURL_CSELECT_IN;
  if (write)
    action |= CURL_CSELECT_OUT;
  if (err)
    action |= CURL_CSELECT_ERR;

  auto result = curl_multi_socket_action(Curl::multi_handle, fd, action, &_ignore);
  FXL_DCHECK(result == CURLM_OK);

  if (cleanup_pending) {
    return;
  }

  cleanup_pending = true;
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, []() {
    cleanup_pending = false;

    int _ignore;
    while (auto info = curl_multi_info_read(Curl::multi_handle, &_ignore)) {
      if (info->msg != CURLMSG_DONE) {
        // CURLMSG_DONE is the only value for msg, documented or otherwise, so this is mostly
        // future-proofing at writing.
        continue;
      }

      Curl* curl;
      auto result = curl_easy_getinfo(info->easy_handle, CURLINFO_PRIVATE, &curl);
      FXL_DCHECK(result == CURLE_OK);

      auto cb = std::move(curl->multi_cb_);
      curl->multi_cb_ = nullptr;
      curl->FreeSList();
      auto rem_result = curl_multi_remove_handle(Curl::multi_handle, info->easy_handle);
      FXL_DCHECK(rem_result == CURLM_OK);

      auto ref = curl->self_ref_;
      curl->self_ref_ = nullptr;

      cb(ref.get(), Curl::Error(info->data.result));
    }
  });
}

// Callback given to CURL which it uses to inform us it would like to do IO on a socket and that we
// should add it to our polling in the event loop.
int SocketCallback(CURL* easy, curl_socket_t s, int what, void*, void*) {
  if (what == CURL_POLL_REMOVE || what == CURL_POLL_NONE) {
    CurlFDWatcher::watches.erase(s);
  } else {
    debug_ipc::MessageLoop::WatchMode mode;

    switch (what) {
      case CURL_POLL_IN:
        mode = debug_ipc::MessageLoop::WatchMode::kRead;
        break;
      case CURL_POLL_OUT:
        mode = debug_ipc::MessageLoop::WatchMode::kWrite;
        break;
      case CURL_POLL_INOUT:
        mode = debug_ipc::MessageLoop::WatchMode::kReadWrite;
        break;
      default:
        FXL_NOTREACHED();
        return -1;
    };

    CurlFDWatcher::watches[s] =
        debug_ipc::MessageLoop::Current()->WatchFD(mode, s, &CurlFDWatcher::instance);
  }

  return 0;
}

// Callback given to CURL which it uses to inform us it would like to receive a timer notification
// at a given time in the future. If the callback is called twice before the timer expires it is
// expected to re-schedule the existing timer, not make a second timer. A timeout of -1 means to
// cancel the outstanding timer.
int TimerCallback(CURLM* multi, long timeout_ms, void*) {
  static std::shared_ptr<bool> last_timer = std::make_shared<bool>();

  *last_timer = false;

  if (timeout_ms < 0) {
    return 0;
  }

  last_timer = std::make_shared<bool>(true);

  debug_ipc::MessageLoop::Current()->PostTimer(
      FROM_HERE, timeout_ms, [multi, valid = last_timer]() {
        if (!*valid) {
          return;
        }

        int _ignore;
        auto result = curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &_ignore);
        FXL_DCHECK(result == CURLM_OK);
      });

  return 0;
}

template <typename T>
void curl_easy_setopt_CHECK(CURL* handle, CURLoption option, T t) {
  auto result = curl_easy_setopt(handle, option, t);
  FXL_DCHECK(result == CURLE_OK);
}

}  // namespace

size_t Curl::global_init = 0;
CURLM* Curl::multi_handle = nullptr;

size_t DoHeaderCallback(char* data, size_t size, size_t nitems, void* curl) {
  Curl* self = reinterpret_cast<Curl*>(curl);
  return self->header_callback_(std::string(data, size * nitems));
}

size_t DoDataCallback(char* data, size_t size, size_t nitems, void* curl) {
  Curl* self = reinterpret_cast<Curl*>(curl);
  return self->data_callback_(std::string(data, size * nitems));
}

Curl::Curl() {
  if (!global_init++) {
    auto res = curl_global_init(CURL_GLOBAL_SSL);
    FXL_DCHECK(!res);
  }

  curl_ = curl_easy_init();
  FXL_DCHECK(curl_);

  // The curl handle has a private pointer which we can stash the address of our wrapper class in.
  // Then anywhere the curl handle appears in the API we can grab our wrapper.
  curl_easy_setopt_CHECK(curl_, CURLOPT_PRIVATE, this);
}

Curl::~Curl() {
  FXL_DCHECK(!multi_cb_);

  if (!curl_) {
    return;
  }

  curl_easy_cleanup(curl_);

  if (!--global_init) {
    if (multi_handle) {
      auto result = curl_multi_cleanup(multi_handle);
      FXL_DCHECK(result == CURLM_OK);
      multi_handle = nullptr;
    }

    curl_global_cleanup();
  }
}

std::shared_ptr<Curl> Curl::MakeShared() {
  auto ret = std::make_shared<Curl>();
  ret->weak_self_ref_ = ret;
  return ret;
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

void Curl::PrepareToPerform() {
  FXL_DCHECK(!multi_cb_);

  curl_easy_setopt_CHECK(curl_, CURLOPT_HEADERFUNCTION, DoHeaderCallback);
  curl_easy_setopt_CHECK(curl_, CURLOPT_HEADERDATA, this);
  curl_easy_setopt_CHECK(curl_, CURLOPT_WRITEFUNCTION, DoDataCallback);
  curl_easy_setopt_CHECK(curl_, CURLOPT_WRITEDATA, this);

  // API documentation specifies "A long value of 1" enables this option, so we convert very
  // specifically. Why take chances on sensible behavior?
  curl_easy_setopt_CHECK(curl_, CURLOPT_NOBODY, get_body_ ? 0L : 1L);

  if (post_data_.empty()) {
    curl_easy_setopt_CHECK(curl_, CURLOPT_POST, 0);
  } else {
    curl_easy_setopt_CHECK(curl_, CURLOPT_POSTFIELDS, post_data_.data());
    curl_easy_setopt_CHECK(curl_, CURLOPT_POSTFIELDSIZE, post_data_.size());
  }

  FXL_DCHECK(!slist_);
  for (const auto& header : headers_) {
    slist_ = curl_slist_append(slist_, header.c_str());
  }

  curl_easy_setopt_CHECK(curl_, CURLOPT_HTTPHEADER, slist_);
}

void Curl::FreeSList() {
  if (slist_)
    curl_slist_free_all(slist_);
  slist_ = nullptr;
}

Curl::Error Curl::Perform() {
  PrepareToPerform();
  auto ret = Error(curl_easy_perform(curl_));
  FreeSList();
  return ret;
}

void Curl::Perform(fit::callback<void(Curl*, Curl::Error)> cb) {
  self_ref_ = weak_self_ref_.lock();
  FXL_DCHECK(self_ref_) << "To use async Curl::Perform you must construct with Curl::MakeShared";

  PrepareToPerform();
  InitMulti();
  auto result = curl_multi_add_handle(multi_handle, curl_);
  FXL_DCHECK(result == CURLM_OK);

  multi_cb_ = std::move(cb);

  int _ignore;
  result = curl_multi_socket_action(multi_handle, CURL_SOCKET_TIMEOUT, 0, &_ignore);
  FXL_DCHECK(result == CURLM_OK);
}

void Curl::InitMulti() {
  if (multi_handle) {
    return;
  }

  multi_handle = curl_multi_init();
  FXL_DCHECK(multi_handle);

  auto result = curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, SocketCallback);
  FXL_DCHECK(result == CURLM_OK);
  result = curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, TimerCallback);
  FXL_DCHECK(result == CURLM_OK);
}

long Curl::ResponseCode() {
  long ret;

  auto result = curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &ret);
  FXL_DCHECK(result == CURLE_OK);
  return ret;
}

}  // namespace zxdb
