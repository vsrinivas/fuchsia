// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_MDNS_SERVICE_IMPL_H_
#define GARNET_BIN_MDNS_SERVICE_MDNS_SERVICE_IMPL_H_

#include <fuchsia/mdns/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_map>

#include "garnet/bin/mdns/service/config.h"
#include "garnet/bin/mdns/service/mdns.h"
#include "src/lib/fxl/macros.h"

namespace mdns {

class MdnsServiceImpl : public fuchsia::mdns::Controller {
 public:
  MdnsServiceImpl(sys::ComponentContext* component_context);

  ~MdnsServiceImpl() override;

  // Controller implementation.
  void ResolveHostName(std::string host_name, int64_t timeout_ns,
                       ResolveHostNameCallback callback) override;

  void SubscribeToService(
      std::string service_name,
      fidl::InterfaceHandle<fuchsia::mdns::ServiceSubscriber> subscriber)
      override;

  void PublishServiceInstance(
      std::string service_name, std::string instance_name, bool perform_probe,
      fidl::InterfaceHandle<fuchsia::mdns::Responder> responder_handle,
      PublishServiceInstanceCallback callback) override;

  void DEPRECATEDPublishServiceInstance(
      std::string service_name, std::string instance_name, uint16_t port,
      std::vector<std::string> text, bool perform_probe,
      PublishServiceInstanceCallback callback) override;

  void DEPRECATEDUnpublishServiceInstance(std::string service_name,
                                          std::string instance_name) override;

  void DEPRECATEDSetVerbose(bool value) override;

 private:
  class Subscriber : public Mdns::Subscriber {
   public:
    Subscriber(fidl::InterfaceHandle<fuchsia::mdns::ServiceSubscriber> handle,
               fit::closure deleter);

    ~Subscriber() override;

    // Mdns::Subscriber implementation:
    void InstanceDiscovered(const std::string& service,
                            const std::string& instance,
                            const inet::SocketAddress& v4_address,
                            const inet::SocketAddress& v6_address,
                            const std::vector<std::string>& text) override;

    void InstanceChanged(const std::string& service,
                         const std::string& instance,
                         const inet::SocketAddress& v4_address,
                         const inet::SocketAddress& v6_address,
                         const std::vector<std::string>& text) override;

    void InstanceLost(const std::string& service,
                      const std::string& instance) override;

   private:
    static constexpr size_t kMaxPipelineDepth = 16;

    enum class EntryType {
      kInstanceDiscovered,
      kInstanceChanged,
      kInstanceLost,
    };

    struct Entry {
      EntryType type;
      fuchsia::mdns::ServiceInstance service_instance;
    };

    // Sends the entry at the head of the queue, if there is one and if
    // |pipeline_depth_| is less than |kMaxPipelineDepth|.
    void MaybeSendNextEntry();

    // Decrements |pipeline_depth_| and calls |MaybeSendNextEntry|.
    void ReplyReceived();

    fuchsia::mdns::ServiceSubscriberPtr client_;
    std::queue<Entry> entries_;
    size_t pipeline_depth_ = 0;

    // Disallow copy, assign and move.
    Subscriber(const Subscriber&) = delete;
    Subscriber(Subscriber&&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    Subscriber& operator=(Subscriber&&) = delete;
  };

  // Publisher for PublishServiceInstance.
  class SimplePublisher : public Mdns::Publisher {
   public:
    SimplePublisher(std::unique_ptr<Mdns::Publication> publication,
                    PublishServiceInstanceCallback callback);

   private:
    // Mdns::Publisher implementation.
    void ReportSuccess(bool success) override;

    void GetPublication(bool query, const std::string& subtype,
                        fit::function<void(std::unique_ptr<Mdns::Publication>)>
                            callback) override;

    std::unique_ptr<Mdns::Publication> publication_;
    PublishServiceInstanceCallback callback_;

    // Disallow copy, assign and move.
    SimplePublisher(const SimplePublisher&) = delete;
    SimplePublisher(SimplePublisher&&) = delete;
    SimplePublisher& operator=(const SimplePublisher&) = delete;
    SimplePublisher& operator=(SimplePublisher&&) = delete;
  };

  // Publisher for AddResponder.
  class ResponderPublisher : public Mdns::Publisher {
   public:
    ResponderPublisher(fuchsia::mdns::ResponderPtr responder,
                       PublishServiceInstanceCallback callback,
                       fit::closure deleter);

    // Mdns::Publisher implementation.
    void ReportSuccess(bool success) override;

    void GetPublication(bool query, const std::string& subtype,
                        fit::function<void(std::unique_ptr<Mdns::Publication>)>
                            callback) override;

    fuchsia::mdns::ResponderPtr responder_;
    PublishServiceInstanceCallback callback_;

    // Disallow copy, assign and move.
    ResponderPublisher(const ResponderPublisher&) = delete;
    ResponderPublisher(ResponderPublisher&&) = delete;
    ResponderPublisher& operator=(const ResponderPublisher&) = delete;
    ResponderPublisher& operator=(ResponderPublisher&&) = delete;
  };

  // Starts the service.
  void Start();

  // Handles a bind request.
  void OnBindRequest(fidl::InterfaceRequest<fuchsia::mdns::Controller> request);

  // Handles the ready callback from |mdns_|.
  void OnReady();

  // Publishes a service instance using |SimplePublisher|.
  bool PublishServiceInstance(std::string service_name,
                              std::string instance_name,
                              std::unique_ptr<Mdns::Publication> publication,
                              bool perform_probe,
                              PublishServiceInstanceCallback callback);

  sys::ComponentContext* component_context_;
  Config config_;
  bool ready_ = false;
  std::vector<fidl::InterfaceRequest<fuchsia::mdns::Controller>>
      pending_binding_requests_;
  fidl::BindingSet<fuchsia::mdns::Controller> bindings_;
  mdns::Mdns mdns_;
  size_t next_subscriber_id_ = 0;
  std::unordered_map<size_t, std::unique_ptr<Subscriber>> subscribers_by_id_;
  std::unordered_map<std::string, std::unique_ptr<Mdns::Publisher>>
      publishers_by_instance_full_name_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MdnsServiceImpl);
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_MDNS_SERVICE_IMPL_H_
