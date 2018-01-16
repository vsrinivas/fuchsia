// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/channel.h>

#include "garnet/bin/mdns/tool/mdns_params.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/macros.h"
#include "lib/mdns/cpp/service_subscriber.h"
#include "lib/mdns/fidl/mdns.fidl.h"

namespace mdns {

class MdnsImpl : public MdnsResponder {
 public:
  MdnsImpl(app::ApplicationContext* application_context, MdnsParams* params);

  ~MdnsImpl() override;

 private:
  void WaitForKeystroke();

  void HandleKeystroke();

  void Resolve(const std::string& host_name, uint32_t timeout_seconds);

  void Subscribe(const std::string& service_name);

  void Publish(const std::string& service_name,
               const std::string& instance_name,
               uint16_t port,
               const std::vector<std::string>& text);

  void Unpublish(const std::string& service_name,
                 const std::string& instance_name);

  void Respond(const std::string& service_name,
               const std::string& instance_name,
               uint16_t port,
               const std::vector<std::string>& announce,
               const std::vector<std::string>& text);

  // MdnsResponder implementation:
  void UpdateStatus(MdnsResult result) override;

  void GetPublication(bool query,
                      const fidl::String& subtype,
                      const GetPublicationCallback& callback) override;

  MdnsServicePtr mdns_service_;
  ServiceSubscriber subscriber_;
  fidl::Binding<MdnsResponder> binding_;
  fsl::FDWaiter fd_waiter_;

  uint16_t publication_port_;
  std::vector<std::string> publication_text_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MdnsImpl);
};

}  // namespace mdns
