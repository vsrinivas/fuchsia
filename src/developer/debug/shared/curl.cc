// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/curl.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/shared/fd_watcher.h"
#include "src/developer/debug/shared/message_loop.h"

namespace debug_ipc {

namespace {

bool global_initialized = false;

template <typename T>
void curl_easy_setopt_CHECK(CURL* handle, CURLoption option, T t) {
  auto result = curl_easy_setopt(handle, option, t);
  FX_DCHECK(result == CURLE_OK);
}

}  // namespace

// All Curl instances share one Curl::Impl instance. RefCountedThreadSafe is used to destroy the
// Impl after the last Curl instance is destructed.
class Curl::Impl final : public debug_ipc::FDWatcher, public fxl::RefCountedThreadSafe<Curl::Impl> {
 public:
  static fxl::RefPtr<Curl::Impl> GetInstance();
  CURLM* multi_handle() { return multi_handle_; }
  void OnFDReady(int fd, bool read, bool write, bool err) override;

 private:
  // Callback given to CURL which it uses to inform us it would like to do IO on a socket and that
  // we should add it to our polling in the event loop.
  static int SocketCallback(CURL* easy, curl_socket_t s, int what, void* userp, void* socketp);

  // Callback given to CURL which it uses to inform us it would like to receive a timer notification
  // at a given time in the future. If the callback is called twice before the timer expires it is
  // expected to re-schedule the existing timer, not make a second timer. A timeout of -1 means to
  // cancel the outstanding timer.
  static int TimerCallback(CURLM* multi, long timeout_ms, void* userp);

  // Impl() will check and set the pointer, and ~Impl() will check and reset it to make sure there's
  // at most 1 instance per thread at a time.
  static thread_local Impl* instance_;

  Impl();
  ~Impl();

  CURLM* multi_handle_;
  std::map<curl_socket_t, debug_ipc::MessageLoop::WatchHandle> watches_;
  // Indicates whether we already have a task posted to read the messages from multi_handler_.
  bool cleanup_pending_ = false;
  // Used in TimerCallback to avoid scheduling 2 timers and invalidate timers after destruction,
  // because currently there's no way to cancel a timer from the message loop.
  std::shared_ptr<bool> last_timer_valid_ = std::make_shared<bool>(false);

  FRIEND_REF_COUNTED_THREAD_SAFE(Impl);
  FRIEND_MAKE_REF_COUNTED(Impl);
};

thread_local Curl::Impl* Curl::Impl::instance_ = nullptr;

fxl::RefPtr<Curl::Impl> Curl::Impl::GetInstance() {
  if (instance_) {
    return fxl::RefPtr<Curl::Impl>(instance_);
  }
  return fxl::MakeRefCounted<Curl::Impl>();
}

void Curl::Impl::OnFDReady(int fd, bool read, bool write, bool err) {
  int action = 0;

  if (read)
    action |= CURL_CSELECT_IN;
  if (write)
    action |= CURL_CSELECT_OUT;
  if (err)
    action |= CURL_CSELECT_ERR;

  int _ignore;
  auto result = curl_multi_socket_action(multi_handle_, fd, action, &_ignore);
  FX_DCHECK(result == CURLM_OK);

  if (cleanup_pending_) {
    return;
  }

  cleanup_pending_ = true;
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [self = fxl::RefPtr<Impl>(this)]() {
    self->cleanup_pending_ = false;

    int _ignore;
    while (auto info = curl_multi_info_read(self->multi_handle_, &_ignore)) {
      if (info->msg != CURLMSG_DONE) {
        // CURLMSG_DONE is the only value for msg, documented or otherwise, so this is mostly
        // future-proofing at writing.
        continue;
      }

      Curl* curl;
      auto result = curl_easy_getinfo(info->easy_handle, CURLINFO_PRIVATE, &curl);
      FX_DCHECK(result == CURLE_OK);

      auto cb = std::move(curl->multi_cb_);
      curl->multi_cb_ = nullptr;
      curl->FreeSList();
      auto rem_result = curl_multi_remove_handle(self->multi_handle_, info->easy_handle);
      FX_DCHECK(rem_result == CURLM_OK);

      auto ref = curl->self_ref_;
      curl->self_ref_ = nullptr;

      cb(ref.get(), Curl::Error(info->data.result));
    }
  });
}

int Curl::Impl::SocketCallback(CURL* /*easy*/, curl_socket_t s, int what, void* /*userp*/,
                               void* /*socketp*/) {
  FX_DCHECK(instance_);

  if (what == CURL_POLL_REMOVE || what == CURL_POLL_NONE) {
    instance_->watches_.erase(s);
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
        FX_NOTREACHED();
        return -1;
    }

    instance_->watches_[s] = debug_ipc::MessageLoop::Current()->WatchFD(mode, s, instance_);
  }

  return 0;
}

int Curl::Impl::TimerCallback(CURLM* multi, long timeout_ms, void* /*userp*/) {
  FX_DCHECK(instance_);

  *instance_->last_timer_valid_ = false;
  // A timeout_ms value of -1 passed to this callback means you should delete the timer.
  if (timeout_ms < 0) {
    return 0;
  }

  instance_->last_timer_valid_ = std::make_shared<bool>(true);
  // It's possible to use PostTask instead of PostTimer if timeout_ms is 0. DO NOT call
  // curl_multi_socket_action directly!
  debug_ipc::MessageLoop::Current()->PostTimer(
      FROM_HERE, timeout_ms, [multi, valid = instance_->last_timer_valid_]() {
        if (!*valid) {
          return;
        }

        int _ignore;
        auto result = curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &_ignore);
        FX_DCHECK(result == CURLM_OK);
      });

  return 0;
}

Curl::Impl::Impl() {
  FX_DCHECK(instance_ == nullptr);
  instance_ = this;

  FX_DCHECK(global_initialized);

  multi_handle_ = curl_multi_init();
  FX_DCHECK(multi_handle_);

  auto result = curl_multi_setopt(multi_handle_, CURLMOPT_SOCKETFUNCTION, SocketCallback);
  FX_DCHECK(result == CURLM_OK);
  result = curl_multi_setopt(multi_handle_, CURLMOPT_TIMERFUNCTION, TimerCallback);
  FX_DCHECK(result == CURLM_OK);
}

Curl::Impl::~Impl() {
  *last_timer_valid_ = false;

  auto result = curl_multi_cleanup(multi_handle_);
  FX_DCHECK(result == CURLM_OK);

  FX_DCHECK(instance_ == this);
  instance_ = nullptr;
}

void Curl::GlobalInit() {
  FX_DCHECK(!global_initialized);
  auto res = curl_global_init(CURL_GLOBAL_SSL);
  FX_DCHECK(!res);
  global_initialized = true;
}

void Curl::GlobalCleanup() {
  FX_DCHECK(global_initialized);
  curl_global_cleanup();
  global_initialized = false;
}

Curl::Curl() {
  impl_ = Impl::GetInstance();
  curl_ = curl_easy_init();
  FX_DCHECK(curl_);

  // The curl handle has a private pointer which we can stash the address of our wrapper class in.
  // Then anywhere the curl handle appears in the API we can grab our wrapper.
  curl_easy_setopt_CHECK(curl_, CURLOPT_PRIVATE, this);
}

Curl::~Curl() {
  FX_DCHECK(!multi_cb_);
  curl_easy_cleanup(curl_);
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

  set_post_data(encoded);
}

std::string Curl::Escape(const std::string& input) {
  // It's legal to pass a null Curl_easy to curl_easy_escape (actually Curl_convert_to_network).
  auto escaped = curl_easy_escape(nullptr, input.c_str(), static_cast<int>(input.size()));
  // std::string(nullptr) is an UB.
  if (!escaped)
    return "";
  std::string ret(escaped);
  curl_free(escaped);
  return ret;
}

void Curl::PrepareToPerform() {
  FX_DCHECK(!multi_cb_);

  // It's critical to convert the lambda into function pointer, because the lambda is only valid in
  // this scope but the function pointer is always valid even without "static".
  using FunctionType = size_t (*)(char* data, size_t size, size_t nitems, void* curl);
  FunctionType DoHeaderCallback = [](char* data, size_t size, size_t nitems, void* curl) {
    return reinterpret_cast<Curl*>(curl)->header_callback_(std::string(data, size * nitems));
  };
  FunctionType DoDataCallback = [](char* data, size_t size, size_t nitems, void* curl) {
    return reinterpret_cast<Curl*>(curl)->data_callback_(std::string(data, size * nitems));
  };

  curl_easy_setopt_CHECK(curl_, CURLOPT_HEADERFUNCTION, DoHeaderCallback);
  curl_easy_setopt_CHECK(curl_, CURLOPT_HEADERDATA, this);
  curl_easy_setopt_CHECK(curl_, CURLOPT_WRITEFUNCTION, DoDataCallback);
  curl_easy_setopt_CHECK(curl_, CURLOPT_WRITEDATA, this);

  // We don't want to set a hard timeout on the request, as the symbol file might be extremely large
  // and the downloading might take arbitrary time.
  // The default connect timeout is 300s, which is too long for today's network.
  curl_easy_setopt_CHECK(curl_, CURLOPT_CONNECTTIMEOUT, 10L);
  // Curl will install some signal handler for SIGPIPE which causes a segfault if NOSIGNAL is unset.
  curl_easy_setopt_CHECK(curl_, CURLOPT_NOSIGNAL, 1L);
  // Abort if slower than 1 bytes/sec during 10 seconds. Ideally we want a read timeout.
  // This will install a lot of timers (one for each read() call) to the message loop.
  curl_easy_setopt_CHECK(curl_, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt_CHECK(curl_, CURLOPT_LOW_SPEED_TIME, 10L);

  // API documentation specifies "A long value of 1" enables this option, so we convert very
  // specifically. Why take chances on sensible behavior?
  curl_easy_setopt_CHECK(curl_, CURLOPT_NOBODY, get_body_ ? 0L : 1L);

  if (post_data_.empty()) {
    curl_easy_setopt_CHECK(curl_, CURLOPT_POST, 0);
  } else {
    curl_easy_setopt_CHECK(curl_, CURLOPT_POSTFIELDS, post_data_.data());
    curl_easy_setopt_CHECK(curl_, CURLOPT_POSTFIELDSIZE, post_data_.size());
  }

  FX_DCHECK(!slist_);
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
  self_ref_ = fxl::RefPtr<Curl>(this);

  PrepareToPerform();
  auto result = curl_multi_add_handle(impl_->multi_handle(), curl_);
  FX_DCHECK(result == CURLM_OK);

  multi_cb_ = std::move(cb);

  int _ignore;
  result = curl_multi_socket_action(impl_->multi_handle(), CURL_SOCKET_TIMEOUT, 0, &_ignore);
  FX_DCHECK(result == CURLM_OK);
}

long Curl::ResponseCode() {
  long ret;

  auto result = curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &ret);
  FX_DCHECK(result == CURLE_OK);
  return ret;
}

}  // namespace debug_ipc
