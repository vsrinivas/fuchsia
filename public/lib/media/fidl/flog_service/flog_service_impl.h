// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_FLOG_FLOG_SERVICE_IMPL_H_
#define MOJO_SERVICES_FLOG_FLOG_SERVICE_IMPL_H_

#include <map>
#include <memory>

#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/services/flog/interfaces/flog.mojom.h"
#include "services/flog/flog_directory.h"
#include "services/util/cpp/factory_service_base.h"
#include "services/util/cpp/incident.h"

namespace mojo {
namespace flog {

// FlogService implementation.
class FlogServiceImpl : public util::FactoryServiceBase, public FlogService {
 public:
  FlogServiceImpl();

  ~FlogServiceImpl() override;

  // ApplicationImplBase overrides.
  void OnInitialize() override;

  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override;

  // FlogService implementation.
  void CreateLogger(InterfaceRequest<FlogLogger> logger,
                    const String& label) override;

  void GetLogDescriptions(const GetLogDescriptionsCallback& callback) override;

  void CreateReader(InterfaceRequest<FlogReader> reader,
                    uint32_t log_id) override;

  void DeleteLog(uint32_t log_id) override;

  void DeleteAllLogs() override;

 private:
  Incident ready_;
  BindingSet<FlogService> bindings_;
  uint32_t last_allocated_log_id_ = 0;
  std::unique_ptr<std::map<uint32_t, std::string>> log_labels_by_id_;
  std::shared_ptr<FlogDirectory> directory_;
};

}  // namespace flog
}  // namespace mojo

#endif  // MOJO_SERVICES_FLOG_FLOG_SERVICE_IMPL_H_
