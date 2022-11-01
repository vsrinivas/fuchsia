// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_H_

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/clock.h>

#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/connectivity/network/mdns/service/agents/address_prober.h"
#include "src/connectivity/network/mdns/service/agents/address_responder.h"
#include "src/connectivity/network/mdns/service/agents/mdns_agent.h"
#include "src/connectivity/network/mdns/service/common/service_instance.h"
#include "src/connectivity/network/mdns/service/encoding/dns_message.h"
#include "src/connectivity/network/mdns/service/transport/mdns_interface_transceiver.h"
#include "src/lib/inet/socket_address.h"

namespace mdns {

class HostNameRequestor;
class InstanceProber;
class InstanceRequestor;
class InstanceResponder;
class ResourceRenewer;

// Implements mDNS.
class Mdns : public MdnsAgent::Owner {
 public:
  // Abstract base class for message transceiver.
  class Transceiver {
   public:
    using InboundMessageCallback =
        fit::function<void(std::unique_ptr<DnsMessage>, const ReplyAddress&)>;
    using InterfaceTransceiverCreateFunction =
        fit::function<std::unique_ptr<MdnsInterfaceTransceiver>(inet::IpAddress, const std::string&,
                                                                uint32_t, Media)>;

    virtual ~Transceiver() {}

    // Starts the transceiver.
    virtual void Start(fuchsia::net::interfaces::WatcherPtr watcher,
                       fit::closure link_change_callback,
                       InboundMessageCallback inbound_message_callback,
                       InterfaceTransceiverCreateFunction transceiver_factory) = 0;

    // Stops the transceiver.
    virtual void Stop() = 0;

    // Determines if this transceiver has interfaces.
    virtual bool HasInterfaces() = 0;

    // Sends a message to the specified address. A V6 interface will send to
    // |MdnsAddresses::V6Multicast| if |reply_address.socket_address()| is
    // |MdnsAddresses::V4Multicast|.
    virtual void SendMessage(const DnsMessage& message, const ReplyAddress& reply_address) = 0;

    // Writes log messages describing lifetime traffic.
    virtual void LogTraffic() = 0;

    // Gets the list of addresses for the local host.
    virtual std::vector<HostAddress> LocalHostAddresses() = 0;
  };

  // Describes an initial instance publication or query response.
  struct Publication {
    static std::unique_ptr<Publication> Create(
        inet::IpPort port,
        const std::vector<std::vector<uint8_t>>& text = std::vector<std::vector<uint8_t>>(),
        uint16_t srv_priority = 0, uint16_t srv_weight = 0);

    std::unique_ptr<Publication> Clone() const;

    inet::IpPort port_;
    std::vector<std::vector<uint8_t>> text_;
    uint16_t srv_priority_ = 0;
    uint16_t srv_weight_ = 0;
    uint32_t ptr_ttl_seconds_ = 120;   // default 2 minutes
    uint32_t srv_ttl_seconds_ = 120;   // default 2 minutes
    uint32_t txt_ttl_seconds_ = 4500;  // default 75 minutes
  };

  using ServiceInstance = ServiceInstance;

  // Abstract base class for client-supplied subscriber.
  class HostNameSubscriber {
   public:
    virtual ~HostNameSubscriber();

    // Unsubscribes from the host name. If this |Subscriber| is already
    // unsubscribed, this method does nothing.
    void Unsubscribe();

    // Called when addresses associated with the host name are changed.
    virtual void AddressesChanged(std::vector<HostAddress> addresses) = 0;

   protected:
    HostNameSubscriber() = default;

   private:
    void Connect(std::shared_ptr<HostNameRequestor> host_name_requestor);

    std::shared_ptr<HostNameRequestor> host_name_requestor_;

    friend class Mdns;
  };

  // Abstract base class for client-supplied subscriber.
  class Subscriber {
   public:
    virtual ~Subscriber();

    // Unsubscribes from the service. If this |Subscriber| is already
    // unsubscribed, this method does nothing.
    void Unsubscribe();

    // Called when a new instance is discovered.
    virtual void InstanceDiscovered(const std::string& service, const std::string& instance,
                                    const std::vector<inet::SocketAddress>& addresses,
                                    const std::vector<std::vector<uint8_t>>& text,
                                    uint16_t srv_priority, uint16_t srv_weight,
                                    const std::string& target) = 0;

    // Called when a previously discovered instance changes addresses or text.
    virtual void InstanceChanged(const std::string& service, const std::string& instance,
                                 const std::vector<inet::SocketAddress>& addresses,
                                 const std::vector<std::vector<uint8_t>>& text,
                                 uint16_t srv_priority, uint16_t srv_weight,
                                 const std::string& target) = 0;

    // Called when an instance is lost.
    virtual void InstanceLost(const std::string& service, const std::string& instance) = 0;

    // Called when a query is sent.
    virtual void Query(DnsType type_queried) = 0;

   protected:
    Subscriber() {}

   private:
    void Connect(std::shared_ptr<InstanceRequestor> instance_requestor);

    std::shared_ptr<InstanceRequestor> instance_requestor_;

    friend class Mdns;
  };

  // Abstract base class for client-supplied publisher.
  class Publisher {
   public:
    using GetPublicationCallback = fit::function<void(std::unique_ptr<Publication>)>;

    virtual ~Publisher();

    // Sets subtypes for the service instance.  If this |Publisher| is
    // unpublished, this method does nothing.
    void SetSubtypes(std::vector<std::string> subtypes);

    // Initiates announcement of the service instance.  If this |Publisher| is
    // unpublished, this method does nothing.
    void Reannounce();

    // Unpublishes the service instance. If this |Publisher| is already
    // unpublished, this method does nothing.
    void Unpublish();

    // Reports whether the publication attempt was successful. Publication can
    // fail if the service instance is currently be published by another device
    // on the subnet.
    virtual void ReportSuccess(bool success) = 0;

    // Provides instance information for initial announcements and query
    // responses relating to the service instance specified in |PublishServiceInstance|.
    // |pubication_type| indicates whether data is requested for an initial announcement
    // or in response to a multicast or unicast query. If the publication relates to
    // a subtype of the service, |subtype| contains the subtype, otherwise it is
    // empty. |source_addresses| supplies the source addresses of the queries that
    // caused this publication. If the publication provided by the callback is null, no
    // announcement or response is transmitted.
    virtual void GetPublication(PublicationCause pubication_type, const std::string& subtype,
                                const std::vector<inet::SocketAddress>& source_addresses,
                                GetPublicationCallback callback) = 0;

   protected:
    Publisher() {}

   private:
    void Connect(std::shared_ptr<InstanceResponder> instance_responder);

    void ConnectProber(std::shared_ptr<InstanceProber> instance_prober);

    void DisconnectProber();

    std::shared_ptr<InstanceResponder> instance_responder_;
    std::shared_ptr<InstanceProber> instance_prober_;

    friend class Mdns;
  };

  // Abstract base class for client-supplied host publisher.
  class HostPublisher {
   public:
    virtual ~HostPublisher();

    // Unpublishes the service instance. If this |Publisher| is already
    // unpublished, this method does nothing.
    void Unpublish();

    // Reports whether the publication attempt was successful. Publication can
    // fail if the service instance is currently being published by another device
    // on the subnet.
    virtual void ReportSuccess(bool success) = 0;

   protected:
    HostPublisher() = default;

   private:
    void Connect(std::shared_ptr<AddressResponder> address_responder);

    void ConnectProber(std::shared_ptr<AddressProber> address_prober);

    void DisconnectProber();

    std::shared_ptr<AddressResponder> address_responder_;
    std::shared_ptr<AddressProber> address_prober_;

    friend class Mdns;
  };

  using ResolveHostNameCallback =
      fit::function<void(const std::string& host_name, std::vector<HostAddress> addresses)>;

  using ResolveServiceInstanceCallback = fit::function<void(fuchsia::net::mdns::ServiceInstance)>;

  // |transceiver| must live as long as this |Mdns| object.
  Mdns(Transceiver& transceiver);

  virtual ~Mdns() override;

  // Determines whether message traffic will be logged.
  void SetVerbose(bool verbose);

  // Starts the transceiver. |ready_callback| is called once we're is ready for
  // calls to |ResolveHostName|, |SubscribeToService| and
  // |PublishServiceInstance|.
  void Start(fuchsia::net::interfaces::WatcherPtr, const std::string& local_host_name,
             bool perform_address_probe, fit::closure ready_callback,
             std::vector<std::string> alt_services);

  // Stops the transceiver.
  void Stop();

  // Returns the local host name currently in use. May be different than the host name
  // passed in to |Start| if address probing detected conflicts.
  std::string local_host_name() { return local_host_name_; }

  // Resolves |host_name| to |IpAddress|es. Must not be called before |Start|'s ready callback is
  // called.
  void ResolveHostName(const std::string& host_name, zx::duration timeout, Media media,
                       IpVersions ip_versions, bool include_local, bool include_local_proxies,
                       ResolveHostNameCallback callback);

  // Subscribers to the specified host name. Must not be called before
  // |Start|'s ready callback is called. The subscription is cancelled when
  // the subscriber is deleted or its |Unsubscribe| method is called.
  // Multiple subscriptions may be created for a given host name.
  void SubscribeToHostName(const std::string& host_name, Media media, IpVersions ip_versions,
                           bool include_local, bool include_local_proxies,
                           HostNameSubscriber* subscriber);

  // Resolves |service+instance| to a node, i.e sends an SRV query and gets
  // a valid response if the service instance exists/is active.
  void ResolveServiceInstance(const std::string& service, const std::string& instance,
                              zx::time timeout, Media media, IpVersions ip_versions,
                              bool include_local, bool include_local_proxies,
                              ResolveServiceInstanceCallback callback);

  // Subscribes to the specified service. The subscription is cancelled when
  // the subscriber is deleted or its |Unsubscribe| method is called.
  // Multiple subscriptions may be created for a given service name. Must not be
  // called before |Start|'s ready callback is called.
  void SubscribeToService(const std::string& service_name, Media media, IpVersions ip_versions,
                          bool include_local, bool include_local_proxies, Subscriber* subscriber);

  // Publishes a service instance. Returns false if and only if the instance was
  // already published locally. The instance is unpublished when the publisher
  // is deleted or its |Unpublish| method is called. Must not be called before
  // |Start|'s ready callback is called.
  bool PublishServiceInstance(std::string service_name, std::string instance_name, Media media,
                              IpVersions ip_versions, bool perform_probe, Publisher* publisher) {
    return PublishServiceInstance("", {}, std::move(service_name), std::move(instance_name), media,
                                  ip_versions, perform_probe, publisher);
  }

  // Publishes a service instance for a host identified by |host_name| and |addresses|. Returns
  // false if and only if the instance was already published locally. The instance is unpublished
  // when the publisher is deleted or its |Unpublish| method is called. Must not be called before
  // |Start|'s ready callback is called.
  bool PublishServiceInstance(std::string host_name, std::vector<inet::IpAddress> addresses,
                              std::string service_name, std::string instance_name, Media media,
                              IpVersions ip_versions, bool perform_probe, Publisher* publisher);

  // Publishes a host. Returns false if and only if the host was already published locally. The
  // host is unpublished when the publisher is deleted or its |Unpublish| method is called. Must
  // not be called for |Start|'s ready callback is called.
  bool PublishHost(std::string host_name, std::vector<inet::IpAddress> addresses, Media media,
                   IpVersions ip_versions, bool perform_probe, HostPublisher* publisher);

  // Writes log messages describing lifetime traffic.
  void LogTraffic();

 private:
  enum class State {
    kNotStarted,
    kWaitingForInterfaces,
    kAddressProbeInProgress,
    kActive,
  };

  struct TaskQueueEntry {
    TaskQueueEntry(MdnsAgent* agent, fit::closure task, zx::time time)
        : agent_(agent), task_(std::move(task)), time_(time) {}

    MdnsAgent* agent_;
    // mutable because std::priority_queue doesn't provide a non-const accessor
    // for its contents which makes it otherwise impossible to move the closure
    // out of the queue when it is time to dispatch the task
    mutable fit::closure task_;
    zx::time time_;

    bool operator<(const TaskQueueEntry& other) const { return time_ > other.time_; }
  };

  struct ReplyAddressHash {
    std::size_t operator()(const ReplyAddress& reply_address) const noexcept {
      return std::hash<inet::SocketAddress>{}(reply_address.socket_address()) ^
             (std::hash<inet::IpAddress>{}(reply_address.interface_address()) << 1) ^
             (std::hash<uint32_t>{}(reply_address.interface_id()) << 2) ^
             (std::hash<Media>{}(reply_address.media()) << 3) ^
             (std::hash<IpVersions>{}(reply_address.ip_versions()) << 4);
    }
  };

  struct ResourceHash {
    std::size_t operator()(std::shared_ptr<DnsResource> resource) const noexcept {
      return resource ? std::hash<DnsResource>{}(*resource) : 0;
    }
  };

  struct ResourceEqual {
    bool operator()(std::shared_ptr<DnsResource> a, std::shared_ptr<DnsResource> b) const noexcept {
      return a ? (b ? *a == *b : false) : !b;
    }
  };

  struct RequestorKey {
    RequestorKey() = default;

    RequestorKey(std::string name, Media media, IpVersions ip_versions)
        : name_(std::move(name)), media_(media), ip_versions_(ip_versions) {}

    bool operator==(const RequestorKey& other) const {
      return name_ == other.name_ && media_ == other.media_ && ip_versions_ == other.ip_versions_;
    }

    std::string name_;
    Media media_;
    IpVersions ip_versions_;
  };

  struct RequestorKeyHash {
    std::size_t operator()(const RequestorKey& value) const noexcept {
      return std::hash<std::string>{}(value.name_) ^ (std::hash<Media>{}(value.media_) << 1) ^
             (std::hash<IpVersions>{}(value.ip_versions_) << 2);
    }
  };

  class DnsMessageBuilder {
   public:
    void AddQuestion(std::shared_ptr<DnsQuestion> question) { questions_.push_back(question); }

    void AddResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section) {
      switch (section) {
        case MdnsResourceSection::kAnswer:
          answers_.insert(resource);
          break;
        case MdnsResourceSection::kAuthority:
          authorities_.insert(resource);
          break;
        case MdnsResourceSection::kAdditional:
          additionals_.insert(resource);
          break;
        case MdnsResourceSection::kExpired:
          FX_DCHECK(false);
          break;
      }
    }

    void Build(DnsMessage& message_out) const {
      if (questions_.empty()) {
        message_out.header_.SetResponse(true);
        message_out.header_.SetAuthoritativeAnswer(true);
      } else {
        message_out.questions_ = std::move(questions_);
      }

      message_out.answers_ =
          std::vector<std::shared_ptr<DnsResource>>(answers_.begin(), answers_.end());
      message_out.authorities_ =
          std::vector<std::shared_ptr<DnsResource>>(authorities_.begin(), authorities_.end());
      message_out.additionals_ =
          std::vector<std::shared_ptr<DnsResource>>(additionals_.begin(), additionals_.end());

      message_out.UpdateCounts();
    }

   private:
    std::vector<std::shared_ptr<DnsQuestion>> questions_;
    std::unordered_set<std::shared_ptr<DnsResource>, ResourceHash, ResourceEqual> answers_;
    std::unordered_set<std::shared_ptr<DnsResource>, ResourceHash, ResourceEqual> authorities_;
    std::unordered_set<std::shared_ptr<DnsResource>, ResourceHash, ResourceEqual> additionals_;
  };

  // Starts the address probe or transitions to ready state, depending on
  // |perform_address_probe|. This method is called the first time a transceiver
  // becomes ready.
  void OnInterfacesStarted(const std::string& local_host_name, bool perform_address_probe);

  // Starts a probe for a conflicting host name. If a conflict is detected, a
  // new name is generated and this method is called again. If no conflict is
  // detected, |local_host_full_name_| gets set and the service is ready to start
  // other agents.
  void StartAddressProbe(const std::string& local_host_name);

  // Sets |local_host_name_|, |local_host_name_full_name_| and |address_placeholder_|.
  void RegisterLocalHostName(const std::string& local_host_name);

  // Starts agents and calls the ready callback. This method is called when
  // at least one transceiver is ready and a unique host name has been
  // established.
  void OnReady();

  // Call |agent->OnAddProxyHost| for each agent in |agents_|.
  void OnAddProxyHost(const std::string& host_full_name, const std::vector<HostAddress>& addresses);

  // Call |agent->OnRemoveProxyHost| for each agent in |agents_|.
  void OnRemoveProxyHost(const std::string& host_full_name);

  // Call |agent->OnAddLocalServiceInstance| for each agent in |agents_|.
  void OnAddLocalServiceInstance(const ServiceInstance& service_instance, bool from_proxy);

  // Call |agent->OnChangeLocalServiceInstance| for each agent in |agents_|.
  void OnChangeLocalServiceInstance(const ServiceInstance& service_instance, bool from_proxy);

  // Call |agent->OnRemoveLocalServiceInstance| for each agent in |agents_|.
  void OnRemoveLocalServiceInstance(const std::string& service_name,
                                    const std::string& instance_name, bool from_proxy);

  // Determines what host name to try next after a conflict is detected and
  // calls |StartAddressProbe| with that name.
  void OnHostNameConflict();

  // MdnsAgent::Owner implementation.
  zx::time now() override;

  void PostTaskForTime(MdnsAgent* agent, fit::closure task, zx::time target_time) override;

  void SendQuestion(std::shared_ptr<DnsQuestion> question, ReplyAddress reply_address) override;

  void SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                    const ReplyAddress& reply_address) override;

  void SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) override;

  void Renew(const DnsResource& resource, Media media, IpVersions ip_versions) override;

  void RemoveAgent(std::shared_ptr<MdnsAgent> agent) override;

  void FlushSentItems() override;

  void AddLocalServiceInstance(const ServiceInstance& instance, bool from_proxy) override;

  void ChangeLocalServiceInstance(const ServiceInstance& instance, bool from_proxy) override;

  std::vector<HostAddress> LocalHostAddresses() override;

  // Adds an agent and, if |started_|, starts it.
  void AddAgent(std::shared_ptr<MdnsAgent> agent);

  // Sends any messages found in |outbound_messages_by_reply_address_| and
  // clears |outbound_messages_by_reply_address_|.
  void SendMessages();

  // Distributes questions to all the agents except the resource renewer.
  void ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address,
                       const ReplyAddress& sender_address);

  // Distributes resources to all the agents, starting with the resource
  // renewer.
  void ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                       ReplyAddress sender_address);

  // Runs tasks in |task_queue_| using |dispatcher_|.
  void PostTask();

  async_dispatcher_t* dispatcher_;
  Transceiver& transceiver_;
  std::string original_local_host_name_;
  fit::closure ready_callback_;
  std::vector<std::string> alt_services_;
  uint32_t next_local_host_name_deduplicator_ = 2;
  std::string local_host_name_;
  std::string local_host_full_name_;
  State state_ = State::kNotStarted;
  std::priority_queue<TaskQueueEntry> task_queue_;
  zx::time posted_task_time_ = zx::time::infinite();
  std::unordered_map<ReplyAddress, DnsMessageBuilder, ReplyAddressHash>
      outbound_message_builders_by_reply_address_;
  std::vector<std::shared_ptr<MdnsAgent>> agents_awaiting_start_;
  std::unordered_set<std::shared_ptr<MdnsAgent>> agents_;
  std::unordered_map<RequestorKey, std::shared_ptr<HostNameRequestor>, RequestorKeyHash>
      host_name_requestors_by_key_;
  std::unordered_map<RequestorKey, std::shared_ptr<InstanceRequestor>, RequestorKeyHash>
      instance_requestors_by_key_;
  std::unordered_map<std::string, std::shared_ptr<InstanceResponder>>
      instance_responders_by_instance_full_name_;
  std::unordered_map<std::string, std::shared_ptr<AddressResponder>>
      address_responders_by_host_full_name_;
  std::shared_ptr<DnsResource> address_placeholder_;
#ifdef MDNS_TRACE
  // Because |verbose_| defaults to true, traffic will be logged as long as the
  // enable_mdns_trace gn arg is set to true. This is preferred, as there is no
  // way (currently) to set |verbose_| to true at runtime.
  bool verbose_ = true;
#endif  // MDNS_TRACE
  std::shared_ptr<ResourceRenewer> resource_renewer_;
  bool prohibit_agent_removal_ = false;
  bool defer_flush_ = false;

#ifdef NDEBUG
#define DPROHIBIT_AGENT_REMOVAL() ((void)0)
#define DALLOW_AGENT_REMOVAL() ((void)0)
#else
#define DPROHIBIT_AGENT_REMOVAL() (prohibit_agent_removal_ = true)
#define DALLOW_AGENT_REMOVAL() (prohibit_agent_removal_ = false)
#endif  // NDEBUG

 public:
  // Disallow copy, assign and move.
  Mdns(const Mdns&) = delete;
  Mdns(Mdns&&) = delete;
  Mdns& operator=(const Mdns&) = delete;
  Mdns& operator=(Mdns&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_H_
