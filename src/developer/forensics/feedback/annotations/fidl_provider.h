// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_FIDL_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_FIDL_PROVIDER_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics::feedback {

// Static async annotation provider that handles calling a single FIDL method and
// returning the result of the call as Annotations when the method completes.
//
// |Interface| is the FIDL protocol being interacted with.
// |method| is the method being called on |Interface|.
// |Convert| is a function object type for converting the results of |method| to Annotations.
template <typename Interface, auto method, typename Convert>
class StaticSingleFidlMethodAnnotationProvider : public StaticAsyncAnnotationProvider {
 public:
  StaticSingleFidlMethodAnnotationProvider(async_dispatcher_t* dispatcher,
                                           std::shared_ptr<sys::ServiceDirectory> services,
                                           std::unique_ptr<backoff::Backoff> backoff);

  void GetOnce(::fit::callback<void(Annotations)> callback) override;

 private:
  void Call();

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<backoff::Backoff> backoff_;
  Convert convert_;

  ::fidl::InterfacePtr<Interface> ptr_;
  ::fit::callback<void(Annotations)> callback_;
  fxl::WeakPtrFactory<StaticSingleFidlMethodAnnotationProvider> ptr_factory_{this};
};

// Dynamic async annotation provider that handles calling a single FIDL method and
// returning the result of the call as Annotations when the method completes.
//
// |Interface| is the FIDL protocol being interacted with.
// |method| is the method being called on |Interface|.
// |Convert| is a function object type for converting the results of |method| to Annotations.
template <typename Interface, auto method, typename Convert>
class DynamicSingleFidlMethodAnnotationProvider : public DynamicAsyncAnnotationProvider {
 public:
  DynamicSingleFidlMethodAnnotationProvider(async_dispatcher_t* dispatcher,
                                            std::shared_ptr<sys::ServiceDirectory> services,
                                            std::unique_ptr<backoff::Backoff> backoff);

  void Get(::fit::callback<void(Annotations)> callback) override;

 private:
  void CleanupCompleted();

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<backoff::Backoff> backoff_;
  Convert convert_;

  ::fidl::InterfacePtr<Interface> ptr_;
  std::vector<::fit::callback<void(Annotations)>> callbacks_;
  fxl::WeakPtrFactory<DynamicSingleFidlMethodAnnotationProvider> ptr_factory_{this};
};

template <typename Interface, auto method, typename Convert>
StaticSingleFidlMethodAnnotationProvider<Interface, method, Convert>::
    StaticSingleFidlMethodAnnotationProvider(async_dispatcher_t* dispatcher,
                                             std::shared_ptr<sys::ServiceDirectory> services,
                                             std::unique_ptr<backoff::Backoff> backoff)
    : dispatcher_(dispatcher), services_(std::move(services)), backoff_(std::move(backoff)) {
  ptr_.set_error_handler([this](const zx_status_t status) {
    FX_LOGS(WARNING) << "Lost connection to " << Interface::Name_;
    async::PostDelayedTask(
        dispatcher_,
        [self = ptr_factory_.GetWeakPtr()] {
          if (self) {
            self->Call();
          }
        },
        backoff_->GetNext());
  });
}

template <typename Interface, auto method, typename Convert>
void StaticSingleFidlMethodAnnotationProvider<Interface, method, Convert>::GetOnce(
    ::fit::callback<void(Annotations)> callback) {
  callback_ = std::move(callback);
  Call();
}

template <typename Interface, auto method, typename Convert>
void StaticSingleFidlMethodAnnotationProvider<Interface, method, Convert>::Call() {
  if (!ptr_.is_bound()) {
    services_->Connect(ptr_.NewRequest(dispatcher_));
  }

  ((*ptr_).*method)([this](auto&&... result) {
    callback_(convert_(result...));
    ptr_.Unbind();
  });
}

template <typename Interface, auto method, typename Convert>
DynamicSingleFidlMethodAnnotationProvider<Interface, method, Convert>::
    DynamicSingleFidlMethodAnnotationProvider(async_dispatcher_t* dispatcher,
                                              std::shared_ptr<sys::ServiceDirectory> services,
                                              std::unique_ptr<backoff::Backoff> backoff)
    : dispatcher_(dispatcher), services_(std::move(services)), backoff_(std::move(backoff)) {
  services_->Connect(ptr_.NewRequest(dispatcher_));

  ptr_.set_error_handler([this](const zx_status_t status) {
    FX_LOGS(WARNING) << "Lost connection to " << Interface::Name_;

    // Complete any outstanding callbacks with a connection error.
    for (auto& callback : callbacks_) {
      if (callback != nullptr) {
        callback(convert_(Error::kConnectionError));
      }
    }

    CleanupCompleted();

    async::PostDelayedTask(
        dispatcher_,
        [self = ptr_factory_.GetWeakPtr()] {
          if (self) {
            self->services_->Connect(self->ptr_.NewRequest(self->dispatcher_));
          }
        },
        backoff_->GetNext());
  });
}

template <typename Interface, auto method, typename Convert>
void DynamicSingleFidlMethodAnnotationProvider<Interface, method, Convert>::Get(
    ::fit::callback<void(Annotations)> callback) {
  // A reconnection is in progress.
  if (!ptr_.is_bound()) {
    callback(convert_(Error::kConnectionError));
    return;
  }

  callbacks_.push_back(callback.share());
  ((*ptr_).*method)([this, callback = std::move(callback)](auto&&... result) mutable {
    if (callback != nullptr) {
      callback(convert_(result...));
    }
    CleanupCompleted();
  });
}

template <typename Interface, auto method, typename Convert>
void DynamicSingleFidlMethodAnnotationProvider<Interface, method, Convert>::CleanupCompleted() {
  callbacks_.erase(std::remove_if(callbacks_.begin(), callbacks_.end(),
                                  [](const ::fit::callback<void(Annotations)>& callback) {
                                    return callback == nullptr;
                                  }),
                   callbacks_.end());
}

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_FIDL_PROVIDER_H_
