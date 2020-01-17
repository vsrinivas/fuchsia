// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_H_

#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/clock.h>

#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/connectivity/network/mdns/service/dns_message.h"
#include "src/connectivity/network/mdns/service/mdns_agent.h"
#include "src/connectivity/network/mdns/service/mdns_transceiver.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/inet/socket_address.h"

namespace mdns {

class InstanceProber;
class InstanceRequestor;
class InstanceResponder;
class ResourceRenewer;

// Implements mDNS.
class Mdns : public MdnsAgent::Host {
 public:
  // Describes an initial instance publication or query response.
  struct Publication {
    static std::unique_ptr<Publication> Create(
        inet::IpPort port, const std::vector<std::string>& text = std::vector<std::string>(),
        uint16_t srv_priority = 0, uint16_t srv_weight = 0);

    std::unique_ptr<Publication> Clone();

    inet::IpPort port_;
    std::vector<std::string> text_;
    uint16_t srv_priority_ = 0;
    uint16_t srv_weight_ = 0;
    uint32_t ptr_ttl_seconds_ = 4500;  // default 75 minutes
    uint32_t srv_ttl_seconds_ = 120;   // default 2 minutes
    uint32_t txt_ttl_seconds_ = 4500;  // default 75 minutes
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
                                    const inet::SocketAddress& v4_address,
                                    const inet::SocketAddress& v6_address,
                                    const std::vector<std::string>& text, uint16_t srv_priority,
                                    uint16_t srv_weight) = 0;

    // Called when a previously discovered instance changes addresses or text.
    virtual void InstanceChanged(const std::string& service, const std::string& instance,
                                 const inet::SocketAddress& v4_address,
                                 const inet::SocketAddress& v6_address,
                                 const std::vector<std::string>& text, uint16_t srv_priority,
                                 uint16_t srv_weight) = 0;

    // Called when an instance is lost.
    virtual void InstanceLost(const std::string& service, const std::string& instance) = 0;

   protected:
    Subscriber() {}

   private:
    void Connect(std::shared_ptr<InstanceRequestor> instance_requestor);

    std::shared_ptr<InstanceRequestor> instance_subscriber_;

    friend class Mdns;
  };

  // Abstract base class for client-supplied publisher.
  class Publisher {
   public:
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
    // responses relating to the service instance specified in |AddResponder|.
    // |query| indicates whether data is requested for an initial announcement
    // (false) or in response to a query (true). If the publication relates to
    // a subtype of the service, |subtype| contains the subtype, otherwise it is
    // empty. If the publication provided by the callback is null, no
    // announcement or response is transmitted.
    virtual void GetPublication(bool query, const std::string& subtype,
                                fit::function<void(std::unique_ptr<Publication>)> callback) = 0;

   protected:
    Publisher() {}

   private:
    void Connect(std::shared_ptr<InstanceResponder> instance_responder);

    std::shared_ptr<InstanceResponder> instance_responder_;

    friend class Mdns;
  };

  using ResolveHostNameCallback =
      fit::function<void(const std::string& host_name, const inet::IpAddress& v4_address,
                         const inet::IpAddress& v6_address)>;

  Mdns();

  virtual ~Mdns() override;

  // Determines whether message traffic will be logged.
  void SetVerbose(bool verbose);

  // Starts the transceiver. |ready_callback| is called once we're is ready for
  // calls to |ResolveHostName|, |SubscribeToService| and
  // |PublishServiceInstance|.
  void Start(fuchsia::netstack::NetstackPtr, const std::string& host_name,
             const MdnsAddresses& addresses, bool perform_address_probe,
             fit::closure ready_callback);

  // Stops the transceiver.
  void Stop();

  // Returns the host name currently in use. May be different than the host name
  // passed in to |Start| if address probing detected conflicts.
  std::string host_name() { return host_name_; }

  // Resolves |host_name| to one or two |IpAddress|es. Must not be called before
  // |Start|'s ready callback is called.
  void ResolveHostName(const std::string& host_name, zx::time timeout,
                       ResolveHostNameCallback callback);

  // Subscribes to the specified service. The subscription is cancelled when
  // the subscriber is deleted or its |Unsubscribe| method is called.
  // Multiple subscriptions may be created for a given service name. Must not be
  // called before |Start|'s ready callback is called.
  void SubscribeToService(const std::string& service_name, Subscriber* subscriber);

  // Publishes a service instance. Returns false if and only if the instance was
  // already published locally. The instance is unpublished when the publisher
  // is deleted or its |Unpublish| method is called. Must not be called before
  // |Start|'s ready callback is called.
  bool PublishServiceInstance(const std::string& service_name, const std::string& instance_name,
                              bool perform_probe, Publisher* publisher);

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
             (std::hash<inet::IpAddress>{}(reply_address.interface_address()) << 1);
    }
  };

  // Starts the address probe or transitions to ready state, depending on
  // |perform_address_probe|. This method is called the first time a transceiver
  // becomes ready.
  void OnInterfacesStarted(const std::string& host_name, bool perform_address_probe);

  // Starts a probe for a conflicting host name. If a conflict is detected, a
  // new name is generated and this method is called again. If no conflict is
  // detected, |host_full_name_| gets set and the service is ready to start
  // other agents.
  void StartAddressProbe(const std::string& host_name);

  // Sets |host_name_|, |host_full_name_| and |address_placeholder_|.
  void RegisterHostName(const std::string& host_name);

  // Starts agents and calls the ready callback. This method is called when
  // at least one transceiver is ready and a unique host name has been
  // established.
  void OnReady();

  // Determines what host name to try next after a conflict is detected and
  // calls |StartAddressProbe| with that name.
  void OnHostNameConflict();

  // MdnsAgent::Host implementation.
  zx::time now() override;

  void PostTaskForTime(MdnsAgent* agent, fit::closure task, zx::time target_time) override;

  void SendQuestion(std::shared_ptr<DnsQuestion> question) override;

  void SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                    const ReplyAddress& reply_address) override;

  void SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) override;

  void Renew(const DnsResource& resource) override;

  void RemoveAgent(const MdnsAgent* agent,
                   const std::string& published_instance_full_name) override;

  // Adds an agent and, if |started_|, starts it.
  void AddAgent(std::shared_ptr<MdnsAgent> agent);

  // Adds an instance responder after performing optional probe for conflicts.
  bool AddInstanceResponder(const std::string& service_name, const std::string& instance_name,
                            inet::IpPort port, std::shared_ptr<InstanceResponder> agent,
                            bool perform_probe);

  // Sends any messages found in |outbound_messages_by_reply_address_| and
  // clears |outbound_messages_by_reply_address_|.
  void SendMessages();

  // Distributes questions to all the agents except the resource renewer.
  void ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address);

  // Distributes resources to all the agents, starting with the resource
  // renewer.
  void ReceiveResource(const DnsResource& resource, MdnsResourceSection section);

  // Runs tasks in |task_queue_| using |dispatcher_|.
  void PostTask();

  async_dispatcher_t* dispatcher_;
  MdnsTransceiver transceiver_;
  std::string original_host_name_;
  const MdnsAddresses* addresses_;
  fit::closure ready_callback_;
  uint32_t next_host_name_deduplicator_ = 2;
  std::string host_name_;
  std::string host_full_name_;
  State state_ = State::kNotStarted;
  std::priority_queue<TaskQueueEntry> task_queue_;
  zx::time posted_task_time_ = zx::time::infinite();
  std::unordered_map<ReplyAddress, DnsMessage, ReplyAddressHash>
      outbound_messages_by_reply_address_;
  std::vector<std::shared_ptr<MdnsAgent>> agents_awaiting_start_;
  std::unordered_map<const MdnsAgent*, std::shared_ptr<MdnsAgent>> agents_;
  std::unordered_map<std::string, std::shared_ptr<InstanceRequestor>>
      instance_subscribers_by_service_name_;
  std::unordered_map<std::string, std::shared_ptr<InstanceResponder>>
      instance_publishers_by_instance_full_name_;
  std::shared_ptr<DnsResource> address_placeholder_;
#ifdef MDNS_TRACE
  // Because |verbose_| defaults to true, traffic will be logged as long as the
  // enable_mdns_trace gn arg is set to true. This is preferred, as there is no
  // way (currently) to set |verbose_| to true at runtime.
  bool verbose_ = true;
#endif  // MDNS_TRACE
  std::shared_ptr<ResourceRenewer> resource_renewer_;
  bool prohibit_agent_removal_ = false;

#ifdef NDEBUG
#define DPROHIBIT_AGENT_REMOVAL() ((void)0)
#define DALLOW_AGENT_REMOVAL() ((void)0)
#else
#define DPROHIBIT_AGENT_REMOVAL() (prohibit_agent_removal_ = true)
#define DALLOW_AGENT_REMOVAL() (prohibit_agent_removal_ = false)
#endif  // NDEBUG

  FXL_DISALLOW_COPY_AND_ASSIGN(Mdns);
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_H_
