// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FLOG_SERVICE_FLOG_LOGGER_IMPL_H_
#define APPS_MEDIA_SERVICES_FLOG_SERVICE_FLOG_LOGGER_IMPL_H_

#include <memory>
#include <vector>

#include "apps/media/interfaces/flog/flog.mojom.h"
#include "apps/media/services/flog_service/flog_directory.h"
#include "apps/media/services/flog_service/flog_service_impl.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace mojo {
namespace flog {

// FlogLogger implementation.
class FlogLoggerImpl : public FlogServiceImpl::ProductBase,
                       private MessageReceiverWithResponderStatus {
 public:
  static std::shared_ptr<FlogLoggerImpl> Create(
      InterfaceRequest<FlogLogger> request,
      uint32_t log_id,
      const std::string& label,
      std::shared_ptr<FlogDirectory> directory,
      FlogServiceImpl* owner);

  ~FlogLoggerImpl() override;

 private:
  FlogLoggerImpl(InterfaceRequest<FlogLogger> request,
                 uint32_t log_id,
                 const std::string& label,
                 std::shared_ptr<FlogDirectory> directory,
                 FlogServiceImpl* owner);

  void WriteData(uint32_t data_size, const void* data);

  // MessageReceiverWithResponderStatus implementation.
  bool Accept(Message* message) override;

  bool AcceptWithResponder(Message* message,
                           MessageReceiverWithStatus* responder) override;

  std::unique_ptr<mojo::internal::Router> router_;
  files::FilePtr file_;
};

}  // namespace flog
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FLOG_SERVICE_FLOG_LOGGER_IMPL_H_
