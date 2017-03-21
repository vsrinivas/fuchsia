// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_COMPONENT_MESSAGE_QUEUE_MANAGER_H_
#define APPS_MODULAR_SRC_COMPONENT_MESSAGE_QUEUE_MANAGER_H_

#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/modular/services/component/message_queue.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/macros.h"

namespace modular {

class MessageQueueStorage;

// This class manages message queues for components. It is meant to be called
// from a ComponentContext instance, and manages the message queues for
// that component instance. The ComponentContext instance is responsible for
// deleting the message queues it has created, otherwise they are persisted.
class MessageQueueManager {
 public:
  MessageQueueManager(ledger::LedgerRepository* ledger_repository);
  ~MessageQueueManager();

  void ObtainMessageQueue(const std::string& component_instance_id,
                          const std::string& queue_name,
                          fidl::InterfaceRequest<MessageQueue> message_queue);

  void DeleteMessageQueue(const std::string& component_instance_id,
                          const std::string& queue_name);

  void GetMessageSender(const std::string& queue_token,
                        fidl::InterfaceRequest<MessageSender> sender);

  // Used by AgentRunner to look for new messages on queues which have a trigger
  // associated with it. If a queue corresponding to
  // |component_instance_id| x |queue_name| does not exist, a new one is
  // created.
  // Registering a new watcher will stomp over existing watcher.
  void RegisterWatcher(const std::string& component_instance_id,
                       const std::string& queue_name,
                       const std::function<void()> callback);
  void DropWatcher(const std::string& component_instance_id,
                   const std::string& queue_name);

 private:
  // Generates a random string to use as a queue token.
  std::string GenerateQueueToken() const;

  // Gets the component instance id and queue name from the ledger for the given
  // queue token.
  void GetComponentInstanceQueueName(
      const std::string& queue_token,
      std::function<void(ledger::Status status,
                         bool found,
                         const std::string& component_instance_id,
                         const std::string& queue_name)> callback);

  // Gets the queue token from the ledger for the given component instance id
  // and queue name.
  void GetQueueToken(
      const std::string& component_instance_id,
      const std::string& queue_name,
      std::function<void(ledger::Status status,
                         bool found,
                         const std::string& queue_token)> callback);

  // If a |MessageQueueStorage| exists for the queue_token returns it, otherwise
  // creates one.
  MessageQueueStorage* GetOrMakeMessageQueueStorage(
      const std::string& component_instance_id,
      const std::string& queue_name,
      const std::string& queue_token);

  // Returns an existing message queue or create one.
  void GetOrMakeMessageQueue(
      const std::string& component_instance_id,
      const std::string& queue_name,
      std::function<void(ledger::Status status, MessageQueueStorage* storage)>
          callback);

  ledger::LedgerPtr ledger_;
  ledger::PagePtr page_;

  // A map of queue_token to |MessageStorageQueue|.
  std::unordered_map<std::string, std::unique_ptr<MessageQueueStorage>>
      message_queues_;

  // A hasher for pairs of strings. Not great.
  class PairHash {
   public:
    std::size_t operator()(std::pair<std::string, std::string> const& p) const {
      std::string s;
      s.append(p.first);
      s.push_back('\0');
      s.append(p.second);
      return std::hash<std::string>{}(s);
    }
  };

  // A map of component instance id and queue name to queue tokens. Entries will
  // only be here while a |MessageQueueStorage| exists.
  std::unordered_map<std::pair<std::string, std::string>, std::string, PairHash>
      message_queue_tokens_;

  // A map of component instance id and queue name to watcher callbacks. If a
  // watcher is registered before a |MessageQueueStorage| exists then it is
  // stashed here until a |MessageQueueStorage| is available.
  std::
      unordered_map<std::pair<std::string, std::string>, ftl::Closure, PairHash>
          pending_watcher_callbacks_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MessageQueueManager);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_COMPONENT_MESSAGE_QUEUE_MANAGER_H_
