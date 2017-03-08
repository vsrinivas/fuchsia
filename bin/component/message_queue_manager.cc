// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/component/message_queue_manager.h"

#include <algorithm>
#include <deque>

#include "apps/modular/services/component/message_queue.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace modular {

class MessageQueueStorage;

// This class implements the |MessageQueue| fidl interface, and is owned by
// |MessageQueueStorage|. It forwards all calls to its owner, and expects its
// owner to manage outstanding |MessageQueue.Receive| calls. It also notifies
// its owner on object destruction.
class MessageQueueConnection : public MessageQueue {
 public:
  explicit MessageQueueConnection(MessageQueueStorage* queue_storage);
  ~MessageQueueConnection() override;

 private:
  // |MessageQueue|
  void Receive(const MessageQueue::ReceiveCallback& callback) override;

  // |MessageQueue|
  void GetToken(const GetTokenCallback& callback) override;

  MessageQueueStorage* const queue_storage_;
};

// Class for managing a particular message queue, its tokens and its storage.
// Implementations of |MessageQueue| and |MessageSender| call into this class to
// manipulate the message queue. Owned by |MessageQueueManager|.
// TODO: Make message queues persistent.
class MessageQueueStorage : public MessageSender {
 public:
  MessageQueueStorage(const std::string& component_instance_id,
                      const std::string& queue_name)
      : component_instance_id_(component_instance_id),
        queue_name_(queue_name) {}

  ~MessageQueueStorage() override {}

  // We store |Receive()| callbacks if the queue is empty. We drop these
  // callbacks if the relevant |MessageQueue| interface dies.
  void Receive(const MessageQueueConnection* const conn,
               const MessageQueue::ReceiveCallback& callback) {
    if (!queue_data_.empty()) {
      callback(queue_data_.front());
      queue_data_.pop_front();
    } else {
      receive_callback_queue_.push_back(make_pair(conn, callback));
    }
  }

  // TODO: Make better tokens.
  std::string GetToken() { return component_instance_id_ + "::" + queue_name_; }

  void AddMessageSenderBinding(
      fidl::InterfaceRequest<MessageSender> sender_request) {
    message_sender_bindings_.AddBinding(std::move(this),
                                        std::move(sender_request));
  }

  void AddMessageQueueBinding(
      fidl::InterfaceRequest<MessageQueue> msg_queue_request) {
    message_queue_bindings_.AddBinding(
        std::make_unique<MessageQueueConnection>(this),
        std::move(msg_queue_request));
  }

  // |MessageQueueConnection| calls this method in its destructor so that we can
  // drop any pending receive callbacks.
  void RemoveMessageQueueConnection(const MessageQueueConnection* const conn) {
    receive_callback_queue_.erase(
        std::remove_if(receive_callback_queue_.begin(),
                       receive_callback_queue_.end(),
                       [conn](const ReceiveCallbackQueueItem& item) -> bool {
                         return conn == item.first;
                       }),
        receive_callback_queue_.end());
  }

  void RegisterWatcher(const std::function<void()> watcher) {
    watcher_ = watcher;
  }

  void DropWatcher() { watcher_ = nullptr; }

 private:
  // |MessageSender|
  void Send(const fidl::String& message) override {
    if (!receive_callback_queue_.empty()) {
      auto& receive_item = receive_callback_queue_.front();
      receive_item.second(message);
      receive_callback_queue_.pop_front();
    } else {
      queue_data_.push_back(message);
    }

    if (watcher_) {
      watcher_();
    }
  }

  std::string component_instance_id_;
  std::string queue_name_;

  std::function<void()> watcher_;

  std::deque<std::string> queue_data_;

  using ReceiveCallbackQueueItem =
      std::pair<const MessageQueueConnection*, MessageQueue::ReceiveCallback>;
  std::deque<ReceiveCallbackQueueItem> receive_callback_queue_;

  // When the |MessageQueue| interface closes, this MessageQueueConnection
  // object gets removed (and destroyed due to unique_ptr semantics), which in
  // turn will notify our RemoveMessageQueueConnection method.
  fidl::BindingSet<MessageQueue, std::unique_ptr<MessageQueueConnection>>
      message_queue_bindings_;

  fidl::BindingSet<MessageSender> message_sender_bindings_;
};

// MessageQueueConnection -----------------------------------------------------

MessageQueueConnection::MessageQueueConnection(
    MessageQueueStorage* queue_storage)
    : queue_storage_(queue_storage) {}

MessageQueueConnection::~MessageQueueConnection() {
  queue_storage_->RemoveMessageQueueConnection(this);
}

void MessageQueueConnection::Receive(
    const MessageQueue::ReceiveCallback& callback) {
  queue_storage_->Receive(this, callback);
}

void MessageQueueConnection::GetToken(const GetTokenCallback& callback) {
  callback(queue_storage_->GetToken());
}

// MessageQueueManager --------------------------------------------------------

MessageQueueManager::MessageQueueManager() {}

MessageQueueManager::~MessageQueueManager() {}

void MessageQueueManager::ObtainMessageQueue(
    const std::string& component_instance_id,
    const std::string& queue_name,
    fidl::InterfaceRequest<MessageQueue> message_queue) {
  ObtainMessageQueueStorage(component_instance_id, queue_name)
      ->AddMessageQueueBinding(std::move(message_queue));
}

void MessageQueueManager::DeleteMessageQueue(
    const std::string& component_instance_id,
    const std::string& queue_name) {
  // 1. Delete the token associated with the message queue.
  auto component_it = component_to_queues_.find(component_instance_id);
  FTL_DCHECK(component_it != component_to_queues_.end());

  const QueueNameToStorageMap& queue_storage_map = component_it->second;
  auto queue_it = queue_storage_map.find(queue_name);
  if (queue_it == queue_storage_map.end()) {
    FTL_LOG(WARNING) << "Cannot delete unknown queue. component_instance_id="
                     << component_instance_id << ", queue=" << queue_name;
    return;
  }

  tokens_to_queues_.erase(queue_it->second->GetToken());

  // 2. Delete the MessageQueueStorage itself, which in turn will close all
  // outstanding MessageSender and MessageQueue interface connections, and
  // delete all messages on the queue permanently.
  component_to_queues_[component_instance_id].erase(queue_name);
}

void MessageQueueManager::GetMessageSender(
    const MessageQueueToken& queue_token,
    fidl::InterfaceRequest<MessageSender> sender) {
  auto it = tokens_to_queues_.find(queue_token);
  if (it == tokens_to_queues_.end()) {
    return;
  }

  ComponentInstanceId instance_id;
  MessageQueueName queue_name;
  std::tie(instance_id, queue_name) = it->second;
  component_to_queues_[instance_id][queue_name]->AddMessageSenderBinding(
      std::move(sender));
}

void MessageQueueManager::RegisterWatcher(
    const std::string& component_instance_id,
    const std::string& queue_name,
    const std::function<void()> callback) {
  ObtainMessageQueueStorage(component_instance_id, queue_name)
      ->RegisterWatcher(callback);
}

void MessageQueueManager::DropWatcher(const std::string& component_instance_id,
                                      const std::string& queue_name) {
  auto storage = GetMessageQueueStorage(component_instance_id, queue_name);
  if (storage == nullptr) {
    // Trying to drop watcher for a queue which does not exist. Do nothing.
    return;
  }
  storage->DropWatcher();
}

MessageQueueStorage* MessageQueueManager::ObtainMessageQueueStorage(
    const std::string& component_instance_id,
    const std::string& queue_name) {
  // Find the message queue storage for this component id, or initialize one if
  // it doesn't exist.
  auto component_it = component_to_queues_.find(component_instance_id);
  if (component_it == component_to_queues_.end()) {
    bool inserted = false;
    std::tie(component_it, inserted) = component_to_queues_.emplace(
        component_instance_id, QueueNameToStorageMap());
    FTL_DCHECK(inserted);
  }

  // Find the particular message queue impl, or initialize one if it doesn't
  // exist.
  QueueNameToStorageMap& component_queues = component_it->second;
  auto queue_storage_it = component_queues.find(queue_name);
  if (queue_storage_it == component_queues.end()) {
    bool inserted = false;
    std::tie(queue_storage_it, inserted) = component_queues.emplace(
        queue_name, std::make_unique<MessageQueueStorage>(component_instance_id,
                                                          queue_name));

    tokens_to_queues_[queue_storage_it->second->GetToken()] =
        std::make_pair(component_instance_id, queue_name);

    FTL_DCHECK(inserted);
  }

  return queue_storage_it->second.get();
}

MessageQueueStorage* MessageQueueManager::GetMessageQueueStorage(
    const std::string& component_instance_id,
    const std::string& queue_name) {
  auto component_it = component_to_queues_.find(component_instance_id);
  if (component_it != component_to_queues_.end()) {
    return nullptr;
  }

  QueueNameToStorageMap& component_queues = component_it->second;
  auto queue_storage_it = component_queues.find(queue_name);
  if (queue_storage_it != component_queues.end()) {
    return nullptr;
  }

  return queue_storage_it->second.get();
}

}  // namespace modular
