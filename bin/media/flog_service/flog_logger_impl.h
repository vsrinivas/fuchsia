// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "garnet/bin/media/flog_service/flog_directory.h"
#include "garnet/bin/media/flog_service/flog_service_impl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/internal/router.h"
#include "lib/media/fidl/flog/flog.fidl.h"

namespace flog {

// FlogLogger implementation.
class FlogLoggerImpl : public FlogServiceImpl::ProductBase,
                       private fidl::MessageReceiverWithResponderStatus {
 public:
  static std::shared_ptr<FlogLoggerImpl> Create(
      fidl::InterfaceRequest<FlogLogger> request,
      uint32_t log_id,
      const std::string& label,
      std::shared_ptr<FlogDirectory> directory,
      FlogServiceImpl* owner);

  ~FlogLoggerImpl() override;

  uint32_t id() { return id_; }

  const std::string& label() { return label_; }

 private:
  FlogLoggerImpl(fidl::InterfaceRequest<FlogLogger> request,
                 uint32_t log_id,
                 const std::string& label,
                 std::shared_ptr<FlogDirectory> directory,
                 FlogServiceImpl* owner);

  void WriteData(uint32_t data_size, const void* data);

  // MessageReceiverWithResponderStatus implementation.
  bool Accept(fidl::Message* message) override;

  bool AcceptWithResponder(fidl::Message* message,
                           fidl::MessageReceiverWithStatus* responder) override;

  uint32_t id_;
  std::string label_;
  std::unique_ptr<fidl::internal::Router> router_;
  fxl::UniqueFD fd_;
};

}  // namespace flog
