// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/component/message_queue_manager.h"

#include <algorithm>
#include <deque>
#include <utility>

#include "lib/component/fidl/message_queue.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/strings/string_printf.h"
#include "peridot/bin/component/persistent_queue.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/storage.h"

namespace modular {

class MessageQueueStorage;

// This class implements the |MessageQueue| fidl interface, and is owned by
// |MessageQueueStorage|. It forwards all calls to its owner, and expects its
// owner to manage outstanding |MessageQueue.Receive| calls. It also notifies
// its owner on object destruction.
//
// Interface is public, because bindings are outside of the class.
class MessageQueueConnection : public MessageQueue {
 public:
  explicit MessageQueueConnection(MessageQueueStorage* queue_storage);
  ~MessageQueueConnection() override;

 private:
  // |MessageQueue|
  void RegisterReceiver(fidl::InterfaceHandle<MessageReader> receiver) override;

  // |MessageQueue|
  void GetToken(const GetTokenCallback& callback) override;

  MessageQueueStorage* const queue_storage_;
};

// Class for managing a particular message queue, its tokens and its storage.
// Implementations of |MessageQueue| and |MessageSender| call into this class to
// manipulate the message queue. Owned by |MessageQueueManager|.
class MessageQueueStorage : MessageSender {
 public:
  MessageQueueStorage(std::string queue_name,
                      std::string queue_token,
                      const std::string& file_name_)
      : queue_name_(std::move(queue_name)),
        queue_token_(std::move(queue_token)),
        queue_data_(file_name_) {}

  ~MessageQueueStorage() override = default;

  void RegisterReceiver(fidl::InterfaceHandle<MessageReader> receiver) {
    if (message_receiver_) {
      FXL_DLOG(WARNING) << "Existing MessageReader is being replaced for "
                           "message queue. queue name="
                        << queue_name_;
    }

    message_receiver_.Bind(std::move(receiver));
    message_receiver_.set_connection_error_handler(
        [this] {
          if (receive_ack_pending_) {
            FXL_DLOG(WARNING)
                << "MessageReceiver closed, but OnReceive acknowledgement still"
                   " pending.";
          }
          message_receiver_.reset();
          receive_ack_pending_ = false;
        });

    MaybeSendNextMessage();
  }

  const std::string& queue_token() const { return queue_token_; }

  void AddMessageSenderBinding(fidl::InterfaceRequest<MessageSender> request) {
    message_sender_bindings_.AddBinding(this, std::move(request));
  }

  void AddMessageQueueBinding(fidl::InterfaceRequest<MessageQueue> request) {
    message_queue_bindings_.AddBinding(
        std::make_unique<MessageQueueConnection>(this), std::move(request));
  }

  void RegisterWatcher(const std::function<void()>& watcher) {
    watcher_ = watcher;
    if (watcher_ && !queue_data_.IsEmpty()) {
      watcher_();
    }
  }

  void DropWatcher() { watcher_ = nullptr; }

 private:
  void MaybeSendNextMessage() {
    if (!message_receiver_ || receive_ack_pending_ || queue_data_.IsEmpty()) {
      return;
    }

    receive_ack_pending_ = true;
    message_receiver_->OnReceive(queue_data_.Peek(), [this] {
      receive_ack_pending_ = false;
      queue_data_.Dequeue();
      MaybeSendNextMessage();
    });
  }

  // |MessageSender|
  void Send(const fidl::String& message) override {
    queue_data_.Enqueue(message);
    MaybeSendNextMessage();
    if (watcher_) {
      watcher_();
    }
  }

  const std::string queue_name_;
  const std::string queue_token_;

  std::function<void()> watcher_;

  PersistentQueue queue_data_;

  bool receive_ack_pending_ = false;
  MessageReaderPtr message_receiver_;

  // When a |MessageQueue| connection closes, the corresponding
  // MessageQueueConnection instance gets removed.
  fidl::BindingSet<MessageQueue, std::unique_ptr<MessageQueueConnection>>
      message_queue_bindings_;

  fidl::BindingSet<MessageSender> message_sender_bindings_;
};

// MessageQueueConnection -----------------------------------------------------

MessageQueueConnection::MessageQueueConnection(
    MessageQueueStorage* const queue_storage)
    : queue_storage_(queue_storage) {}

MessageQueueConnection::~MessageQueueConnection() = default;

void MessageQueueConnection::RegisterReceiver(
    fidl::InterfaceHandle<MessageReader> receiver) {
  queue_storage_->RegisterReceiver(std::move(receiver));
}

void MessageQueueConnection::GetToken(const GetTokenCallback& callback) {
  callback(queue_storage_->queue_token());
}

// MessageQueueManager --------------------------------------------------------

namespace {

std::string GenerateQueueToken() {
  // Get 256 bits of pseudo-randomness.
  uint8_t randomness[256 / 8];
  size_t random_size;
  zx_cprng_draw(&randomness, sizeof randomness, &random_size);
  // TODO(alhaad): is there a more efficient way to do this?
  std::string token;
  for (uint8_t byte : randomness) {
    fxl::StringAppendf(&token, "%X", byte);
  }
  return token;
}

}  // namespace

struct MessageQueueManager::MessageQueueInfo {
  std::string component_namespace;
  std::string component_instance_id;
  std::string queue_name;
  std::string queue_token;

  bool is_complete() const {
    return !component_instance_id.empty() && !queue_name.empty();
  }
};

class MessageQueueManager::GetQueueTokenCall : Operation<fidl::String> {
 public:
  GetQueueTokenCall(OperationContainer* const container,
                    ledger::Page* const page,
                    std::string component_namespace,
                    std::string component_instance_id,
                    const std::string& queue_name,
                    ResultCall result_call)
      : Operation("MessageQueueManager::GetQueueTokenCall",
                  container,
                  std::move(result_call),
                  queue_name),
        page_(page),
        component_namespace_(std::move(component_namespace)),
        component_instance_id_(std::move(component_instance_id)),
        queue_name_(queue_name) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &result_};
    page_->GetSnapshot(
        snapshot_.NewRequest(), nullptr, nullptr,
        [this, flow](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FXL_LOG(ERROR) << "Ledger.GetSnapshot() " << status;
            return;
          }

          snapshot_.set_connection_error_handler(
              [] { FXL_LOG(WARNING) << "Error on snapshot connection"; });

          key_ = MakeMessageQueueTokenKey(component_namespace_,
                                          component_instance_id_, queue_name_);
          snapshot_->Get(to_array(key_), [this, flow](ledger::Status status,
                                                      zx::vmo value) {
            if (status == ledger::Status::KEY_NOT_FOUND) {
              // Key wasn't found, that's not an error.
              return;
            }

            if (status != ledger::Status::OK) {
              FXL_LOG(ERROR) << "Failed to get key " << key_;
              return;
            }

            if (!value) {
              FXL_LOG(ERROR) << "Key " << key_ << " has no value";
              return;
            }

            std::string queue_token;
            if (!fsl::StringFromVmo(value, &queue_token)) {
              FXL_LOG(ERROR)
                  << "VMO for key " << key_ << " couldn't be copied.";
              return;
            }
            result_ = queue_token;
          });
        });
  }

  ledger::Page* const page_;  // not owned
  const std::string component_namespace_;
  const std::string component_instance_id_;
  const std::string queue_name_;
  ledger::PageSnapshotPtr snapshot_;
  std::string key_;

  fidl::String result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetQueueTokenCall);
};

class MessageQueueManager::GetMessageSenderCall : Operation<> {
 public:
  GetMessageSenderCall(OperationContainer* const container,
                       MessageQueueManager* const message_queue_manager,
                       ledger::Page* const page,
                       std::string token,
                       fidl::InterfaceRequest<MessageSender> request)
      : Operation("MessageQueueManager::GetMessageSenderCall",
                  container,
                  [] {}),
        message_queue_manager_(message_queue_manager),
        page_(page),
        token_(std::move(token)),
        request_(std::move(request)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    page_->GetSnapshot(
        snapshot_.NewRequest(), nullptr, nullptr,
        [this, flow](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FXL_LOG(ERROR) << "Failed to get snapshot for page";
            return;
          }

          std::string key = MakeMessageQueueKey(token_);
          snapshot_->Get(to_array(key), [this, flow](ledger::Status status,
                                                     zx::vmo value) {
            if (status != ledger::Status::OK) {
              if (status != ledger::Status::KEY_NOT_FOUND) {
                // It's expected that the key is not found when the link
                // is accessed for the first time. Don't log an error
                // then.
                FXL_LOG(ERROR) << "GetMessageSenderCall() " << token_
                               << " PageSnapshot.Get() " << status;
              }
              return;
            }

            std::string value_as_string;
            if (value) {
              if (!fsl::StringFromVmo(value, &value_as_string)) {
                FXL_LOG(ERROR) << "Unable to extract data.";
                return;
              }
            }

            if (!XdrRead(value_as_string, &result_, XdrMessageQueueInfo)) {
              return;
            }

            if (!result_.is_complete()) {
              FXL_LOG(WARNING) << "Queue token " << result_.queue_token
                               << " not found in the ledger.";
              return;
            }

            message_queue_manager_->GetMessageQueueStorage(result_)
                ->AddMessageSenderBinding(std::move(request_));
          });
        });
  }

  MessageQueueManager* const message_queue_manager_;  // not owned
  ledger::Page* const page_;                          // not owned
  const std::string token_;
  fidl::InterfaceRequest<MessageSender> request_;

  ledger::PageSnapshotPtr snapshot_;
  std::string key_;

  MessageQueueInfo result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetMessageSenderCall);
};

class MessageQueueManager::ObtainMessageQueueCall : Operation<> {
 public:
  ObtainMessageQueueCall(OperationContainer* const container,
                         MessageQueueManager* const message_queue_manager,
                         ledger::Page* const page,
                         const std::string& component_namespace,
                         const std::string& component_instance_id,
                         const std::string& queue_name,
                         fidl::InterfaceRequest<MessageQueue> request)
      : Operation("MessageQueueManager::ObtainMessageQueueCall",
                  container,
                  [] {},
                  queue_name),
        message_queue_manager_(message_queue_manager),
        page_(page),
        request_(std::move(request)) {
    message_queue_info_.component_namespace = component_namespace;
    message_queue_info_.component_instance_id = component_instance_id;
    message_queue_info_.queue_name = queue_name;
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    new GetQueueTokenCall(
        &operation_collection_, page_, message_queue_info_.component_namespace,
        message_queue_info_.component_instance_id,
        message_queue_info_.queue_name, [this, flow](fidl::String token) {
          if (token) {
            // Queue token was found in the ledger.
            message_queue_info_.queue_token = token.get();
            Finish(flow);
            return;
          }

          Cont(flow);
        });
  }

  void Cont(FlowToken flow) {
    // Not found in the ledger, time to create a new message
    // queue.
    message_queue_info_.queue_token = GenerateQueueToken();

    page_->StartTransaction([](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FXL_LOG(ERROR) << "Page.StartTransaction() status=" << status;
      }
    });

    const std::string message_queue_token_key =
        MakeMessageQueueTokenKey(message_queue_info_.component_namespace,
                                 message_queue_info_.component_instance_id,
                                 message_queue_info_.queue_name);

    page_->Put(to_array(message_queue_token_key),
               to_array(message_queue_info_.queue_token),
               [key = message_queue_token_key](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FXL_LOG(ERROR)
                       << "Page.Put() " << key << ", status=" << status;
                 }
               });

    const std::string message_queue_key =
        MakeMessageQueueKey(message_queue_info_.queue_token);

    std::string json;
    XdrWrite(&json, &message_queue_info_, XdrMessageQueueInfo);

    page_->Put(
        to_array(message_queue_key),
        to_array(json), [key = message_queue_key](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FXL_LOG(ERROR) << "Page.Put() " << key << ", status=" << status;
          }
        });

    page_->Commit([this, flow](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FXL_LOG(ERROR) << "Page.Commit() status=" << status;
        return;
      }

      FXL_LOG(INFO) << "Created message queue: "
                    << message_queue_info_.queue_token;

      Finish(flow);
    });
  }

  void Finish(FlowToken /*flow*/) {
    message_queue_manager_->GetMessageQueueStorage(message_queue_info_)
        ->AddMessageQueueBinding(std::move(request_));
  }

  MessageQueueManager* const message_queue_manager_;  // not owned
  ledger::Page* const page_;                          // not owned
  fidl::InterfaceRequest<MessageQueue> request_;

  MessageQueueInfo message_queue_info_;
  ledger::PageSnapshotPtr snapshot_;

  OperationCollection operation_collection_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ObtainMessageQueueCall);
};

class MessageQueueManager::DeleteMessageQueueCall : Operation<> {
 public:
  DeleteMessageQueueCall(OperationContainer* const container,
                         MessageQueueManager* const message_queue_manager,
                         ledger::Page* const page,
                         const std::string& component_namespace,
                         const std::string& component_instance_id,
                         const std::string& queue_name)
      : Operation("MessageQueueManager::DeleteMessageQueueCall",
                  container,
                  [] {},
                  queue_name),
        message_queue_manager_(message_queue_manager),
        page_(page) {
    message_queue_info_.component_namespace = component_namespace;
    message_queue_info_.component_instance_id = component_instance_id;
    message_queue_info_.queue_name = queue_name;
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    new GetQueueTokenCall(
        &operation_collection_, page_, message_queue_info_.component_namespace,
        message_queue_info_.component_instance_id,
        message_queue_info_.queue_name, [this, flow](fidl::String token) {
          if (!token) {
            FXL_LOG(WARNING)
                << "Request to delete queue " << message_queue_info_.queue_name
                << " for component instance "
                << message_queue_info_.component_instance_id
                << " that wasn't found in the ledger";
            return;
          }

          message_queue_info_.queue_token = token.get();

          std::string message_queue_key =
              MakeMessageQueueKey(message_queue_info_.queue_token);

          std::string message_queue_token_key = MakeMessageQueueTokenKey(
              message_queue_info_.component_namespace,
              message_queue_info_.component_instance_id,
              message_queue_info_.queue_name);

          // Delete the ledger entries.
          page_->StartTransaction([](ledger::Status status) {
            if (status != ledger::Status::OK) {
              FXL_LOG(ERROR) << "Page.StartTransaction() status=" << status;
            }
          });

          page_->Delete(
              to_array(message_queue_key), [key = message_queue_key](
                                               ledger::Status status) {
                if (status != ledger::Status::OK) {
                  FXL_LOG(ERROR)
                      << "Page.Delete() " << key << ", status=" << status;
                }
              });
          page_->Delete(
              to_array(message_queue_token_key), [key =
                                                      message_queue_token_key](
                                                     ledger::Status status) {
                if (status != ledger::Status::OK) {
                  FXL_LOG(ERROR)
                      << "Page.Delete() " << key << ", status=" << status;
                }
              });

          message_queue_manager_->ClearMessageQueueStorage(message_queue_info_);

          page_->Commit([this, flow](ledger::Status status) {
            if (status != ledger::Status::OK) {
              FXL_LOG(ERROR) << "Page.Commit() status=" << status;
              return;
            }

            FXL_LOG(INFO) << "Deleted queue: "
                          << message_queue_info_.component_instance_id << "/"
                          << message_queue_info_.queue_name;
          });
        });
  }

  MessageQueueManager* const message_queue_manager_;  // not owned
  ledger::Page* const page_;                          // not owned
  MessageQueueInfo message_queue_info_;
  ledger::PageSnapshotPtr snapshot_;

  OperationCollection operation_collection_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteMessageQueueCall);
};

class MessageQueueManager::DeleteNamespaceCall : Operation<> {
 public:
  DeleteNamespaceCall(OperationContainer* const container,
                      MessageQueueManager* const /*message_queue_manager*/,
                      ledger::Page* const page,
                      const std::string& component_namespace,
                      std::function<void()> done)
      : Operation("MessageQueueManager::DeleteNamespaceCall",
                  container,
                  std::move(done),
                  component_namespace),
        page_(page),
        message_queues_key_prefix_(
            MakeMessageQueuesPrefix(component_namespace)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};
    page_->GetSnapshot(
        snapshot_.NewRequest(), to_array(message_queues_key_prefix_), nullptr,
        [this, flow](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FXL_LOG(ERROR) << "Page.GetSnapshot() status=" << status;
            return;
          }
          GetKeysToDelete(flow);
        });
  }

  void GetKeysToDelete(FlowToken flow) {
    GetEntries(snapshot_.get(), &component_entries_,
               [this, flow](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FXL_LOG(ERROR) << "GetEntries() status=" << status;
                   return;
                 }

                 for (const auto& entry : component_entries_) {
                   FXL_DCHECK(entry->value) << "Value vmo handle is null";

                   keys_to_delete_.push_back(entry->key.Clone());

                   std::string queue_token;
                   if (!fsl::StringFromVmo(entry->value, &queue_token)) {
                     FXL_LOG(ERROR) << "VMO for key " << to_string(entry->key)
                                    << " couldn't be copied.";
                     continue;
                   }

                   keys_to_delete_.push_back(
                       to_array(MakeMessageQueueKey(queue_token)));
                 }

                 DeleteKeys(flow);
               });
  }

  void DeleteKeys(FlowToken flow) {
    for (auto& i : keys_to_delete_) {
      page_->Delete(i.Clone(), [this, &i, flow](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FXL_LOG(ERROR) << "Page.Delete() " << to_string(i)
                         << ", status=" << status;
        }
      });
    }
  }

  ledger::Page* const page_;  // not owned
  ledger::PageSnapshotPtr snapshot_;
  const std::string message_queues_key_prefix_;
  std::vector<ledger::EntryPtr> component_entries_;
  std::vector<LedgerPageKey> keys_to_delete_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteNamespaceCall);
};

MessageQueueManager::MessageQueueManager(LedgerClient* const ledger_client,
                                         LedgerPageId page_id,
                                         std::string local_path)
    : PageClient("MessageQueueManager",
                 ledger_client,
                 std::move(page_id)),
      local_path_(std::move(local_path)) {}

MessageQueueManager::~MessageQueueManager() = default;

void MessageQueueManager::ObtainMessageQueue(
    const std::string& component_namespace,
    const std::string& component_instance_id,
    const std::string& queue_name,
    fidl::InterfaceRequest<MessageQueue> request) {
  new ObtainMessageQueueCall(&operation_collection_, this, page(),
                             component_namespace, component_instance_id,
                             queue_name, std::move(request));
}

template <typename ValueType>
const ValueType* MessageQueueManager::FindQueueName(
    const ComponentQueueNameMap<ValueType>& queue_map,
    const MessageQueueInfo& info) {
  auto it1 = queue_map.find(info.component_namespace);
  if (it1 != queue_map.end()) {
    auto it2 = it1->second.find(info.component_instance_id);
    if (it2 != it1->second.end()) {
      auto it3 = it2->second.find(info.queue_name);
      if (it3 != it2->second.end()) {
        return &(it3->second);
      }
    }
  }

  return nullptr;
}

template <typename ValueType>
void MessageQueueManager::EraseQueueName(
    ComponentQueueNameMap<ValueType>& queue_map,
    const MessageQueueInfo& info) {
  auto it1 = queue_map.find(info.component_namespace);
  if (it1 != queue_map.end()) {
    auto it2 = it1->second.find(info.component_instance_id);
    if (it2 != it1->second.end()) {
      it2->second.erase(info.queue_name);
    }
  }
}

MessageQueueStorage* MessageQueueManager::GetMessageQueueStorage(
    const MessageQueueInfo& info) {
  auto it = message_queues_.find(info.queue_token);
  if (it == message_queues_.end()) {
    // Not found, create one.
    bool inserted;
    std::string path = local_path_;
    path.push_back('/');
    path.append(info.queue_token);
    path.append(".json");
    auto new_queue = std::make_unique<MessageQueueStorage>(
        info.queue_name, info.queue_token, std::move(path));
    std::tie(it, inserted) = message_queues_.insert(
        std::make_pair(info.queue_token, std::move(new_queue)));
    FXL_DCHECK(inserted);

    message_queue_tokens_[info.component_namespace][info.component_instance_id]
                         [info.queue_name] = info.queue_token;

    const std::function<void()>* const watcher =
        FindQueueName(pending_watcher_callbacks_, info);
    if (watcher) {
      it->second->RegisterWatcher(*watcher);
      EraseQueueName(pending_watcher_callbacks_, info);
    }
  }
  return it->second.get();
}

void MessageQueueManager::ClearMessageQueueStorage(
    const MessageQueueInfo& info) {
  // Remove the |MessageQueueStorage| and delete it which in turn will
  // close all outstanding MessageSender and MessageQueue interface
  // connections, and delete all messages on the queue permanently.
  message_queues_.erase(info.queue_token);

  // Clear entries in message_queue_tokens_ and
  // pending_watcher_callbacks_.
  EraseQueueName(pending_watcher_callbacks_, info);
  EraseQueueName(message_queue_tokens_, info);
}

void MessageQueueManager::DeleteMessageQueue(
    const std::string& component_namespace,
    const std::string& component_instance_id,
    const std::string& queue_name) {
  new DeleteMessageQueueCall(&operation_collection_, this, page(),
                             component_namespace, component_instance_id,
                             queue_name);
}

void MessageQueueManager::DeleteNamespace(
    const std::string& component_namespace,
    std::function<void()> done) {
  new DeleteNamespaceCall(&operation_collection_, this, page(),
                          component_namespace, std::move(done));
}

void MessageQueueManager::GetMessageSender(
    const std::string& queue_token,
    fidl::InterfaceRequest<MessageSender> request) {
  const auto& it = message_queues_.find(queue_token);
  if (it != message_queues_.cend()) {
    // Found the message queue already.
    it->second->AddMessageSenderBinding(std::move(request));
    return;
  }

  new GetMessageSenderCall(&operation_collection_, this, page(), queue_token,
                           std::move(request));
}

void MessageQueueManager::RegisterWatcher(
    const std::string& component_namespace,
    const std::string& component_instance_id,
    const std::string& queue_name,
    const std::function<void()>& watcher) {
  const std::string* token =
      FindQueueName(message_queue_tokens_,
                    MessageQueueInfo{component_namespace, component_instance_id,
                                     queue_name, ""});
  if (!token) {
    pending_watcher_callbacks_[component_namespace][component_instance_id]
                              [queue_name] = watcher;
    return;
  }

  auto msq_it = message_queues_.find(*token);
  FXL_DCHECK(msq_it != message_queues_.end());
  msq_it->second->RegisterWatcher(watcher);
}

void MessageQueueManager::DropWatcher(const std::string& component_namespace,
                                      const std::string& component_instance_id,
                                      const std::string& queue_name) {
  auto queue_info = MessageQueueInfo{component_namespace, component_instance_id,
                                     queue_name, ""};

  const std::string* token = FindQueueName(message_queue_tokens_, queue_info);
  if (token) {
    // The |MessageQueueStorage| doesn't exist yet.
    EraseQueueName(message_queue_tokens_, queue_info);
    return;
  }

  auto msq_it = message_queues_.find(*token);
  if (msq_it == message_queues_.end()) {
    FXL_LOG(WARNING) << "Asked to DropWatcher for a queue that doesn't exist";
    return;
  };
  msq_it->second->DropWatcher();
}

void MessageQueueManager::XdrMessageQueueInfo(XdrContext* const xdr,
                                              MessageQueueInfo* const data) {
  xdr->Field("component_namespace", &data->component_namespace);
  xdr->Field("component_instance_id", &data->component_instance_id);
  xdr->Field("queue_name", &data->queue_name);
  xdr->Field("queue_token", &data->queue_token);
}

}  // namespace modular
