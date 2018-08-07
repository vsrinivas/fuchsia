// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_MESSAGE_QUEUE_MESSAGE_QUEUE_MANAGER_H_
#define PERIDOT_BIN_USER_RUNNER_MESSAGE_QUEUE_MESSAGE_QUEUE_MANAGER_H_

#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {

class MessageQueueStorage;
struct MessageQueueInfo;

// Manages message queues for components. One MessageQueueManager
// instance is used by all ComponentContextImpl instances, and manages
// the message queues for all component instances. The
// fuchsia::modular::ComponentContext instance is responsible for deleting the
// message queues it has created, otherwise they are persisted.
class MessageQueueManager : PageClient {
 public:
  MessageQueueManager(LedgerClient* ledger_client,
                      fuchsia::ledger::PageId page_id, std::string local_path);
  ~MessageQueueManager() override;

  // An enum describing the types of events that can be watched via
  // |RegisterWatcher|.
  enum WatcherEventType {
    // Triggers when there is a new message on the watched messsage queue.
    NEW_MESSAGE,
    // Triggers when the watched message queue is deleted.
    QUEUE_DELETED,
  };

  void ObtainMessageQueue(
      const std::string& component_namespace,
      const std::string& component_instance_id, const std::string& queue_name,
      fidl::InterfaceRequest<fuchsia::modular::MessageQueue> request);

  void DeleteMessageQueue(const std::string& component_namespace,
                          const std::string& component_instance_id,
                          const std::string& queue_name);

  void DeleteNamespace(const std::string& component_namespace,
                       std::function<void()> done);

  void GetMessageSender(
      const std::string& queue_token,
      fidl::InterfaceRequest<fuchsia::modular::MessageSender> request);

  // Registers a watcher that will be called when there is a new message on a
  // queue corresponding to |component_namespace| x |component_instance_id| x
  // |queue_name|.
  //
  // |component_namespace| is the namespace of the watching component (i.e. the
  //   creator of the queue).
  // |component_instance_id| is the namespace of the watching component (i.e.
  //   the creator of the queue).
  // |queue_name| is the name of the message queue.
  //
  // Only one message watcher can be active for a given queue, and registering a
  // new one will remove any existing watcher.
  void RegisterMessageWatcher(const std::string& component_namespace,
                              const std::string& component_instance_id,
                              const std::string& queue_name,
                              const std::function<void()>& watcher);

  // Registers a watcher that gets notified when a message queue with
  // |queue_token| is deleted.
  //
  // Only one deletion watcher can be active for a given queue, and registering
  // a new one will remove any existing watcher.
  //
  // |watcher_namespace| is the namespace of the component that is watching the
  //   message queue deletion.
  // |watcher_instance_id| is the instance id of the component that is watching
  //   the message queue deletion.
  // |queue_token| is the message queue token for the queue to be observed.
  // |watcher| is the callback that will be triggered.
  //
  // Note that this is different from |RegisterMessageWatcher|, where the passed
  // in namespace, instance ids, and queue name directly describe the queue.
  void RegisterDeletionWatcher(const std::string& watcher_namespace,
                               const std::string& watcher_instance_id,
                               const std::string& queue_token,
                               const std::function<void()>& watcher);

  // Drops the watcher for |component_namespace| x |component_instance_id| x
  // |queue_name|.
  void DropMessageWatcher(const std::string& component_namespace,
                          const std::string& component_instance_id,
                          const std::string& queue_name);

  // Drops the watcher described by |queue_info| from watching for the
  // deletion of the queue with |queue_token|.
  void DropDeletionWatcher(const std::string& watcher_namespace,
                           const std::string& watcher_instance_id,
                           const std::string& queue_token);

 private:
  using ComponentNamespace = std::string;
  using ComponentInstanceId = std::string;
  using ComponentQueueName = std::string;
  template <typename Value>
  using ComponentQueueNameMap = std::map<
      ComponentNamespace,
      std::map<ComponentInstanceId, std::map<ComponentQueueName, Value>>>;

  using DeletionWatchers =
      std::map<std::string, std::map<std::string, std::function<void()>>>;

  // Returns the |MessageQueueStorage| for the queue_token. Creates it
  // if it doesn't exist yet.
  MessageQueueStorage* GetMessageQueueStorage(const MessageQueueInfo& info);

  // Clears the |MessageQueueStorage| for the queue_token.
  void ClearMessageQueueStorage(const MessageQueueInfo& info);

  // Clears the |MessageQueueStorage| for all the queues in the provided
  // component namespace.
  void ClearMessageQueueStorage(const std::string& component_namespace);

  // |FindQueueName()| and |EraseQueueName()| are helpers used to operate on
  // component (namespace, id, queue name) mappings.
  // If the given message queue |info| is found the stored pointer value, or
  // nullptr otherwise.
  template <typename ValueType>
  const ValueType* FindQueueName(
      const ComponentQueueNameMap<ValueType>& queue_map,
      const MessageQueueInfo& info);

  // Erases the |ValueType| stored under the provided |info|.
  // Implementation is in the .cc.
  template <typename ValueType>
  void EraseQueueName(ComponentQueueNameMap<ValueType>& queue_map,
                      const MessageQueueInfo& info);
  // Erases all |ValueType|s under the provided namespace.
  // Implementation is in the .cc.
  template <typename ValueType>
  void EraseNamespace(ComponentQueueNameMap<ValueType>& queue_map,
                      const std::string& component_namespace);

  const std::string local_path_;

  // A map of queue_token to |MessageQueueStorage|.
  std::map<std::string, std::unique_ptr<MessageQueueStorage>> message_queues_;

  // A map of queue_token to |MessageQueueInfo|. This allows for easy lookup of
  // message queue information for registering watchers that take message queue
  // tokens as parameters.
  std::map<std::string, MessageQueueInfo> message_queue_infos_;

  // A map of component instance id and queue name to queue tokens.
  // Entries are only here while a |MessageQueueStorage| exists.
  ComponentQueueNameMap<std::string> message_queue_tokens_;

  // A map of component instance id and queue name to watcher
  // callbacks. If a watcher is registered before a
  // |MessageQueueStorage| exists then it is stashed here until a
  // |MessageQueueStorage| is available.
  ComponentQueueNameMap<std::function<void()>> pending_watcher_callbacks_;

  // A map containing watchers that are to be notified when the described
  // message queue is deleted.
  ComponentQueueNameMap<DeletionWatchers> deletion_watchers_;

  OperationCollection operation_collection_;

  // Operations implemented here.
  class GetQueueTokenCall;
  class GetMessageSenderCall;
  class ObtainMessageQueueCall;
  class DeleteMessageQueueCall;
  class DeleteNamespaceCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageQueueManager);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_MESSAGE_QUEUE_MESSAGE_QUEUE_MANAGER_H_
