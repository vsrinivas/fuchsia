// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_COMPONENT_MESSAGE_QUEUE_MANAGER_H_
#define PERIDOT_BIN_COMPONENT_MESSAGE_QUEUE_MANAGER_H_

#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>

#include "lib/async/cpp/operation.h"
#include <fuchsia/cpp/modular.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fxl/macros.h"
#include <fuchsia/cpp/ledger.h>
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {

class MessageQueueStorage;
class XdrContext;

// Manages message queues for components. One MessageQueueManager
// instance is used by all ComponentContextImpl instances, and manages
// the message queues for all component instances. The
// ComponentContext instance is responsible for deleting the message
// queues it has created, otherwise they are persisted.
class MessageQueueManager : PageClient {
 public:
  MessageQueueManager(LedgerClient* ledger_client,
                      ledger::PageId page_id,
                      std::string local_path);
  ~MessageQueueManager();

  // An enum describing the types of events that can be watched via
  // |RegisterWatcher|.
  enum WatcherEventType {
    // Triggers when there is a new message on the watched messsage queue.
    NEW_MESSAGE,
    // Triggers when the watched message queue is deleted.
    QUEUE_DELETED,
  };

  void ObtainMessageQueue(const std::string& component_namespace,
                          const std::string& component_instance_id,
                          const std::string& queue_name,
                          fidl::InterfaceRequest<MessageQueue> request);

  void DeleteMessageQueue(const std::string& component_namespace,
                          const std::string& component_instance_id,
                          const std::string& queue_name);

  void DeleteNamespace(const std::string& component_namespace,
                       std::function<void()> done);

  void GetMessageSender(const std::string& queue_token,
                        fidl::InterfaceRequest<MessageSender> request);

  // Used by AgentRunner to look for new messages on queues which have
  // a trigger associated with it. If a queue corresponding to
  // |component_namespace| x |component_instance_id| x |queue_name| does not
  // exist, a new one is created.
  //
  // Registering a new watcher stomps over any existing watcher.
  void RegisterWatcher(const std::string& component_namespace,
                       const std::string& component_instance_id,
                       const std::string& queue_name,
                       WatcherEventType event_type,
                       const std::function<void()>& watcher);
  // Drops the specified watcher for |event_type|.
  //
  // |queue_name| is the name of the queue, which is required for dropping
  // WatcherEventType::NEW_MESSAGE. |queuE_token| is the token of the queue,
  // which is required for dropping WatherEventType::QUEUE_DELETED.
  void DropWatcher(const std::string& component_namespace,
                   const std::string& component_instance_id,
                   const std::string& queue_name,
                   const std::string& queue_token,
                   WatcherEventType event_type);

 private:
  struct MessageQueueInfo;
  using ComponentNamespace = std::string;
  using ComponentInstanceId = std::string;
  using ComponentQueueName = std::string;
  template <typename Value>
  using ComponentQueueNameMap = std::map<
      ComponentNamespace,
      std::map<ComponentInstanceId, std::map<ComponentQueueName, Value>>>;

  // A pair where first is a representation of the component watching, and
  // second is the watch function associated with that watcher.
  using DeletionWatcher = std::pair<MessageQueueInfo, std::function<void()>>;

  static void XdrMessageQueueInfo(XdrContext* xdr, MessageQueueInfo* data);

  // Drops the watcher described by |queue_info| from watching for new
  // messages on |queue_name|.
  void DropMessageWatcher(const MessageQueueInfo& queue_info,
                          const std::string& queue_name);

  // Drops the watcher described by |queue_info| from watching for the
  // deletion of the queue with |queue_token|.
  void DropDeletionWatcher(const MessageQueueInfo& queue_info,
                           const std::string& queue_token);

  // Returns the |MessageQueueStorage| for the queue_token. Creates it
  // if it doesn't exist yet.
  MessageQueueStorage* GetMessageQueueStorage(const MessageQueueInfo& info);

  // Clears the |MessageQueueStorage| for the queue_token.
  void ClearMessageQueueStorage(const MessageQueueInfo& info);

  // |FindQueueName()| and |EraseQueueName()| are helpers used to operate on
  // component (namespace, id, queue name) mappings.
  // If the given message queue |info| is found the stored pointer value, or
  // nullptr otherwise.
  template <typename ValueType>
  const ValueType* FindQueueName(
      const ComponentQueueNameMap<ValueType>& queue_map,
      const MessageQueueInfo& info);

  template <typename ValueType>
  void EraseQueueName(ComponentQueueNameMap<ValueType>& queue_map,
                      const MessageQueueInfo& info);

  const std::string local_path_;

  // A map of queue_token to |MessageQueueStorage|.
  std::map<std::string, std::unique_ptr<MessageQueueStorage>> message_queues_;

  // A map of component instance id and queue name to queue tokens.
  // Entries are only here while a |MessageQueueStorage| exists.
  ComponentQueueNameMap<std::string> message_queue_tokens_;

  // A map of component instance id and queue name to watcher
  // callbacks. If a watcher is registered before a
  // |MessageQueueStorage| exists then it is stashed here until a
  // |MessageQueueStorage| is available.
  ComponentQueueNameMap<std::function<void()>> pending_watcher_callbacks_;

  OperationCollection operation_collection_;

  // A map from message queue token to any associated watchers that are to be
  // notified when the queue is deleted.
  std::map<std::string, std::vector<DeletionWatcher>> deletion_watchers_;

  // Operations implemented here.
  class GetQueueTokenCall;
  class GetMessageSenderCall;
  class ObtainMessageQueueCall;
  class DeleteMessageQueueCall;
  class DeleteNamespaceCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageQueueManager);
};

}  // namespace modular

#endif  // PERIDOT_BIN_COMPONENT_MESSAGE_QUEUE_MANAGER_H_
