// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_FLOG_FLOG_LOGGER_IMPL_H_
#define MOJO_SERVICES_FLOG_FLOG_LOGGER_IMPL_H_

#include <memory>
#include <vector>

#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/services/flog/interfaces/flog.mojom.h"
#include "services/flog/flog_directory.h"
#include "services/flog/flog_service_impl.h"

namespace mojo {
namespace flog {

// FlogLogger implementation.
class FlogLoggerImpl : public FlogServiceImpl::ProductBase,
                       private MessageReceiverWithResponderStatus,
                       private FlogLogger {
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

  // FlogLogger implementation (called by stub_).
  void LogMojoLoggerMessage(int64_t time_us,
                            int32_t log_level,
                            const String& message,
                            const String& source_file,
                            uint32_t source_line) override;

  void LogChannelCreation(int64_t time_us,
                          uint32_t channel_id,
                          const String& type_name,
                          uint64_t subject_address) override;

  void LogChannelMessage(int64_t time_us,
                         uint32_t channel_id,
                         mojo::Array<uint8_t> data) override;

  void LogChannelDeletion(int64_t time_us, uint32_t channel_id) override;

  std::unique_ptr<mojo::internal::Router> router_;
  files::FilePtr file_;
  FlogLoggerStub stub_;
};

}  // namespace flog
}  // namespace mojo

#endif  // MOJO_SERVICES_FLOG_FLOG_LOGGER_IMPL_H_
