// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_COMPONENT_MESSAGE_QUEUE_MANAGER_H_
#define APPS_MODULAR_SRC_COMPONENT_MESSAGE_QUEUE_MANAGER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "apps/modular/services/component/message_queue.fidl.h"
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
  MessageQueueManager();
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
  using ComponentInstanceId = std::string;
  using MessageQueueToken = std::string;
  using MessageQueueName = std::string;

  using QueueNameToStorageMap =
      std::unordered_map<MessageQueueName,
                         std::unique_ptr<MessageQueueStorage>>;

  MessageQueueStorage* ObtainMessageQueueStorage(
      const std::string& component_instance_id, const std::string& queue_name);
  MessageQueueStorage* GetMessageQueueStorage(
      const std::string& component_instance_id, const std::string& queue_name);

  std::unordered_map<ComponentInstanceId, QueueNameToStorageMap>
      component_to_queues_;
  std::unordered_map<MessageQueueToken,
                     std::pair<ComponentInstanceId, MessageQueueName>>
      tokens_to_queues_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MessageQueueManager);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_COMPONENT_MESSAGE_QUEUE_MANAGER_H_
