// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/curl.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/shared/message_loop.h"

namespace debug {

namespace {

bool global_initialized = false;

template <typename T>
void curl_easy_setopt_CHECK(CURL* handle, CURLoption option, T t) {
  auto result = curl_easy_setopt(handle, option, t);
  FX_DCHECK(result == CURLE_OK);
}

}  // namespace

// All Curl instances share one Curl::MultiHandle instance. RefCountedThreadSafe is used to destroy
// the MultiHandle after the last Curl instance is destructed.
class Curl::MultiHandle final : public fxl::RefCountedThreadSafe<Curl::MultiHandle> {
 public:
  static fxl::RefPtr<Curl::MultiHandle> GetInstance();

  // Adds an easy handle and starts the transfer. The ownership of the easy handle will be shared
  // by this class when the transfer is in progress.
  void AddEasyHandle(Curl* curl);

 private:
  // Function given to CURL which it uses to inform us it would like to do IO on a socket and that
  // we should add it to our polling in the event loop.
  static int SocketFunction(CURL* easy, curl_socket_t s, int what, void* userp, void* socketp);

  // Function given to CURL which it uses to inform us it would like to receive a timer notification
  // at a given time in the future. If the callback is called twice before the timer expires it is
  // expected to re-schedule the existing timer, not make a second timer. A timeout of -1 means to
  // cancel the outstanding timer.
  static int TimerFunction(CURLM* multi, long timeout_ms, void* userp);

  // MultiHandle() will check and set the pointer, and ~MultiHandle() will check and reset it to
  // make sure there's at most 1 instance per thread at a time.
  static thread_local MultiHandle* instance_;

  MultiHandle();
  ~MultiHandle();

  void ProcessResponses();

  CURLM* multi_handle_;

  // Manages the ownership of WatchHandles.
  std::map<curl_socket_t, MessageLoop::WatchHandle> watches_;

  // Manages the ownership of easy handles to prevent them from being destructed when an async
  // transfer is in progress.
  std::map<CURL*, fxl::RefPtr<Curl>> easy_handles_;

  // Indicates whether we already have a task posted to process the messages in multi_handler_.
  bool process_pending_ = false;

  // Used in TimerFunction to avoid scheduling 2 timers and invalidate timers after destruction,
  // because currently there's no way to cancel a timer from the message loop.
  std::shared_ptr<bool> last_timer_valid_ = std::make_shared<bool>(false);

  FRIEND_REF_COUNTED_THREAD_SAFE(MultiHandle);
  FRIEND_MAKE_REF_COUNTED(MultiHandle);
};

thread_local Curl::MultiHandle* Curl::MultiHandle::instance_ = nullptr;

fxl::RefPtr<Curl::MultiHandle> Curl::MultiHandle::GetInstance() {
  if (instance_) {
    return fxl::RefPtr<Curl::MultiHandle>(instance_);
  }
  return fxl::MakeRefCounted<Curl::MultiHandle>();
}

void Curl::MultiHandle::AddEasyHandle(Curl* curl) {
  easy_handles_.emplace(curl->curl_, fxl::RefPtr<Curl>(curl));
  auto result = curl_multi_add_handle(multi_handle_, curl->curl_);
  FX_DCHECK(result == CURLM_OK);

  // There's a chance that the response is available immediately in curl_multi_add_handle, which
  // could happen when the server is localhost, e.g. requesting authentication from metadata server
  // on GCE. In this case, no SocketFunction will be invoked and we have to call ProcessResponses()
  // manually.
  ProcessResponses();
}

void Curl::MultiHandle::ProcessResponses() {
  if (process_pending_) {
    return;
  }
  process_pending_ = true;

  MessageLoop::Current()->PostTask(FROM_HERE, [self = fxl::RefPtr<MultiHandle>(this)]() {
    self->process_pending_ = false;

    int _ignore;
    while (auto info = curl_multi_info_read(self->multi_handle_, &_ignore)) {
      if (info->msg != CURLMSG_DONE) {
        // CURLMSG_DONE is the only value for msg, documented or otherwise, so this is mostly
        // future-proofing at writing.
        continue;
      }

      auto easy_handle_it = self->easy_handles_.find(info->easy_handle);
      FX_DCHECK(easy_handle_it != self->easy_handles_.end());
      fxl::RefPtr<Curl> curl = std::move(easy_handle_it->second);
      curl->FreeSList();
      self->easy_handles_.erase(easy_handle_it);

      // The document says WARNING: The data the returned pointer points to will not survive
      // calling curl_multi_cleanup, curl_multi_remove_handle or curl_easy_cleanup.
      CURLcode code = info->data.result;
      auto result = curl_multi_remove_handle(self->multi_handle_, info->easy_handle);
      FX_DCHECK(result == CURLM_OK);
      // info is invalid now.

      curl->multi_cb_(curl.get(), Curl::Error(code));
      // curl->multi_cb_ becomes nullptr now because fit::callback can only be called once.
    }
  });
}

int Curl::MultiHandle::SocketFunction(CURL* /*easy*/, curl_socket_t s, int what, void* /*userp*/,
                                      void* /*socketp*/) {
  FX_DCHECK(instance_);

  if (what == CURL_POLL_REMOVE || what == CURL_POLL_NONE) {
    instance_->watches_.erase(s);
  } else {
    MessageLoop::WatchMode mode;

    switch (what) {
      case CURL_POLL_IN:
        mode = MessageLoop::WatchMode::kRead;
        break;
      case CURL_POLL_OUT:
        mode = MessageLoop::WatchMode::kWrite;
        break;
      case CURL_POLL_INOUT:
        mode = MessageLoop::WatchMode::kReadWrite;
        break;
      default:
        FX_NOTREACHED();
        return -1;
    }

    instance_->watches_[s] = MessageLoop::Current()->WatchFD(
        mode, s,
        [self = fxl::RefPtr<MultiHandle>(instance_)](int fd, bool read, bool write, bool err) {
          int action = 0;
          if (read)
            action |= CURL_CSELECT_IN;
          if (write)
            action |= CURL_CSELECT_OUT;
          if (err)
            action |= CURL_CSELECT_ERR;

          // curl_multi_socket_action might stop watching when the transfer is done, which will
          // destroy this closure and invalidate the self pointer. Copy it into a variable to
          // prolong its life.
          auto prolonged = self;

          int _ignore;
          auto result = curl_multi_socket_action(prolonged->multi_handle_, fd, action, &_ignore);
          FX_DCHECK(result == CURLM_OK);

          prolonged->ProcessResponses();
        });
  }

  return 0;
}

int Curl::MultiHandle::TimerFunction(CURLM* /*multi*/, long timeout_ms, void* /*userp*/) {
  FX_DCHECK(instance_);

  *instance_->last_timer_valid_ = false;
  // A timeout_ms value of -1 passed to this callback means you should delete the timer.
  if (timeout_ms < 0) {
    return 0;
  }

  instance_->last_timer_valid_ = std::make_shared<bool>(true);
  auto cb = [self = fxl::RefPtr<MultiHandle>(instance_), valid = instance_->last_timer_valid_]() {
    if (!*valid) {
      return;
    }

    // curl_multi_socket_action might stop watching when the transfer is done, which will destroy
    // this closure and invalidate the self pointer. Copy it into a variable to prolong its life.
    auto prolonged = self;

    int _ignore;
    auto result =
        curl_multi_socket_action(prolonged->multi_handle_, CURL_SOCKET_TIMEOUT, 0, &_ignore);
    FX_DCHECK(result == CURLM_OK);

    prolonged->ProcessResponses();
  };

  if (timeout_ms == 0) {
    MessageLoop::Current()->PostTask(FROM_HERE, std::move(cb));
  } else {
    MessageLoop::Current()->PostTimer(FROM_HERE, timeout_ms, std::move(cb));
  }

  return 0;
}

Curl::MultiHandle::MultiHandle() {
  FX_DCHECK(instance_ == nullptr);
  instance_ = this;

  FX_DCHECK(global_initialized);

  multi_handle_ = curl_multi_init();
  FX_DCHECK(multi_handle_);

  auto result = curl_multi_setopt(multi_handle_, CURLMOPT_SOCKETFUNCTION, SocketFunction);
  FX_DCHECK(result == CURLM_OK);
  result = curl_multi_setopt(multi_handle_, CURLMOPT_TIMERFUNCTION, TimerFunction);
  FX_DCHECK(result == CURLM_OK);
}

Curl::MultiHandle::~MultiHandle() {
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
  curl_ = curl_easy_init();
  FX_DCHECK(curl_);
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
  PrepareToPerform();
  multi_cb_ = std::move(cb);
  MultiHandle::GetInstance()->AddEasyHandle(this);
}

long Curl::ResponseCode() {
  long ret;

  auto result = curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &ret);
  FX_DCHECK(result == CURLE_OK);
  return ret;
}

}  // namespace debug
