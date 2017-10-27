// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "garnet/bin/netconnector/mdns/dns_message.h"
#include "garnet/bin/netconnector/mdns/mdns_agent.h"
#include "garnet/bin/netconnector/mdns/mdns_transceiver.h"
#include "garnet/bin/netconnector/mdns/resource_renewer.h"
#include "garnet/bin/netconnector/mdns/responder.h"
#include "garnet/bin/netconnector/socket_address.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_point.h"
#include "lib/netconnector/fidl/mdns.fidl.h"

namespace netconnector {
namespace mdns {

// Implements mDNS.
class Mdns : public MdnsAgent::Host {
 public:
  using ResolveHostNameCallback =
      std::function<void(const std::string& host_name,
                         const IpAddress& v4_address,
                         const IpAddress& v6_address)>;
  using ServiceInstanceCallback =
      std::function<void(const std::string& service,
                         const std::string& instance,
                         const SocketAddress& v4_address,
                         const SocketAddress& v6_address,
                         const std::vector<std::string>& text)>;

  Mdns();

  virtual ~Mdns() override;

  // Enables the specified interface and family. Should be called before calling
  // |Start|. If |EnableInterface| isn't called prior to |Start|, |Mdns| will
  // use all available interfaces. Otherwise it uses just the interfaces that
  // have been enabled.
  void EnableInterface(const std::string& name, sa_family_t family);

  // Determines whether message traffic will be logged.
  void SetVerbose(bool verbose);

  // Starts the transceiver. |callback| is called when addresss probing is
  // complete, and a unique host name has been selected.
  void Start(const std::string& host_name, const fxl::Closure& callback);

  // Stops the transceiver.
  void Stop();

  // Returns the host name currently in use. May be different than the host name
  // passed in to |Start| if address probing detected conflicts.
  std::string host_name() { return host_name_; }

  // Resolves |host_name| to one or two |IpAddress|es.
  void ResolveHostName(const std::string& host_name,
                       fxl::TimePoint timeout,
                       const ResolveHostNameCallback& callback);

  // Starts publishing the indicated service instance. Returns false if and only
  // if the instance was already published.
  bool PublishServiceInstance(const std::string& service_name,
                              const std::string& instance_name,
                              IpPort port,
                              const std::vector<std::string>& text);

  // Stops publishing the indicated service instance. Returns true if and only
  // if the instance existed.
  bool UnpublishServiceInstance(const std::string& service_name,
                                const std::string& instance_name);

  // Registers interest in the specified service.
  std::shared_ptr<MdnsAgent> SubscribeToService(
      const std::string& service_name,
      const ServiceInstanceCallback& callback);

  // Adds a responder. Returns false if and only if the instance was already
  // published. Returns false if and only if the instance was already published.
  bool AddResponder(const std::string& service_name,
                    const std::string& instance_name,
                    fidl::InterfaceHandle<MdnsResponder> responder);

  // Sets subtypes for a service instance currently being published due to a
  // call to |PublishServiceInstance| or |AddResponder|.  Returns true if and
  // only if the instance exists.
  bool SetSubtypes(const std::string& service_name,
                   const std::string& instance_name,
                   std::vector<std::string> subtypes);

  // Initiates announcement of a service instance currently being published due
  // to a call to |PublishServiceInstance| or |AddResponder|. Returns true if
  // and only if the instance exists.
  bool ReannounceInstance(const std::string& service_name,
                          const std::string& instance_name);

 private:
  struct TaskQueueEntry {
    TaskQueueEntry(MdnsAgent* agent, fxl::Closure task, fxl::TimePoint time)
        : agent_(agent), task_(task), time_(time) {}

    MdnsAgent* agent_;
    fxl::Closure task_;
    fxl::TimePoint time_;

    bool operator<(const TaskQueueEntry& other) const {
      return time_ > other.time_;
    }
  };

  struct ReplyAddressHash {
    std::size_t operator()(const ReplyAddress& reply_address) const noexcept {
      size_t hash = reply_address.interface_index();

      const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(
          reply_address.socket_address().as_sockaddr());

      for (socklen_t i = 0; i < reply_address.socket_address().socklen(); ++i) {
        hash = (hash << 1) ^ *byte_ptr;
        ++byte_ptr;
      }

      return hash;
    }
  };

  // Starts a probe for a conflicting host name. If a conflict is detected, a
  // new name is generated and this method is called again. If no conflict is
  // detected, |host_full_name_| gets set and the service is ready to start
  // other agents.
  void StartAddressProbe(const std::string& host_name);

  // Determines what host name to try next after a conflict is detected and
  // calls |StartAddressProbe| with that name.
  void OnHostNameConflict();

  // MdnsAgent::Host implementation.
  void PostTaskForTime(MdnsAgent* agent,
                       fxl::Closure task,
                       fxl::TimePoint target_time) override;

  void SendQuestion(std::shared_ptr<DnsQuestion> question) override;

  void SendResource(std::shared_ptr<DnsResource> resource,
                    MdnsResourceSection section,
                    const ReplyAddress& reply_address) override;

  void SendAddresses(MdnsResourceSection section,
                     const ReplyAddress& reply_address) override;

  void Renew(const DnsResource& resource) override;

  void RemoveAgent(const MdnsAgent* agent,
                   const std::string& published_instance_full_name) override;

  // Adds an agent and, if |started_|, starts it.
  void AddAgent(std::shared_ptr<MdnsAgent> agent);

  // Sends any messages found in |outbound_messages_by_reply_address_| and
  // clears |outbound_messages_by_reply_address_|.
  void SendMessages();

  // Distributes questions to all the agents except the resource renewer.
  void ReceiveQuestion(const DnsQuestion& question,
                       const ReplyAddress& reply_address);

  // Distributes resources to all the agents, starting with the resource
  // renewer.
  void ReceiveResource(const DnsResource& resource,
                       MdnsResourceSection section);

  // Runs tasks in |task_queue_| using |task_runner_|.
  void PostTask();

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  MdnsTransceiver transceiver_;
  std::string original_host_name_;
  fxl::Closure start_callback_;
  uint32_t next_host_name_deduplicator_ = 2;
  std::string host_name_;
  std::string host_full_name_;
  bool started_ = false;
  std::priority_queue<TaskQueueEntry> task_queue_;
  fxl::TimePoint posted_task_time_ = fxl::TimePoint::Max();
  std::unordered_map<ReplyAddress, DnsMessage, ReplyAddressHash>
      outbound_messages_by_reply_address_;
  std::vector<std::shared_ptr<MdnsAgent>> agents_awaiting_start_;
  std::unordered_map<const MdnsAgent*, std::shared_ptr<MdnsAgent>> agents_;
  std::unordered_map<std::string, std::shared_ptr<Responder>>
      instance_publishers_by_instance_full_name_;
  std::shared_ptr<DnsResource> address_placeholder_;
  bool verbose_ = false;
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
}  // namespace netconnector
