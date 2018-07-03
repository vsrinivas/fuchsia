// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/message_queue/message_queue_manager.h"

#include <algorithm>
#include <deque>
#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/strings/string_printf.h>

#include "peridot/bin/user_runner/message_queue/persistent_queue.h"
#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/page_client.h"

namespace modular {

struct MessageQueueInfo {
  std::string component_namespace;
  std::string component_instance_id;
  std::string queue_name;
  std::string queue_token;

  bool is_complete() const {
    return !component_instance_id.empty() && !queue_name.empty();
  }

  bool operator==(const MessageQueueInfo& a) const {
    return component_namespace == a.component_namespace &&
           component_instance_id == a.component_instance_id &&
           queue_name == a.queue_name && queue_token == a.queue_token;
  }
};

namespace {

void XdrMessageQueueInfo_v1(XdrContext* const xdr,
                            MessageQueueInfo* const data) {
  xdr->Field("component_namespace", &data->component_namespace);
  xdr->Field("component_instance_id", &data->component_instance_id);
  xdr->Field("queue_name", &data->queue_name);
  xdr->Field("queue_token", &data->queue_token);
}

void XdrMessageQueueInfo_v2(XdrContext* const xdr,
                            MessageQueueInfo* const data) {
  if (!xdr->Version(2)) {
    return;
  }
  xdr->Field("component_namespace", &data->component_namespace);
  xdr->Field("component_instance_id", &data->component_instance_id);
  xdr->Field("queue_name", &data->queue_name);
  xdr->Field("queue_token", &data->queue_token);
}

constexpr XdrFilterType<MessageQueueInfo> XdrMessageQueueInfo[] = {
    XdrMessageQueueInfo_v2,
    XdrMessageQueueInfo_v1,
    nullptr,
};

}  // namespace

class MessageQueueStorage;

// This class implements the |fuchsia::modular::MessageQueue| fidl interface,
// and is owned by |MessageQueueStorage|. It forwards all calls to its owner,
// and expects its owner to manage outstanding
// |fuchsia::modular::MessageQueue.Receive| calls. It also notifies its owner on
// object destruction.
//
// Interface is public, because bindings are outside of the class.
class MessageQueueConnection : public fuchsia::modular::MessageQueue {
 public:
  explicit MessageQueueConnection(MessageQueueStorage* queue_storage);
  ~MessageQueueConnection() override;

 private:
  // |fuchsia::modular::MessageQueue|
  void RegisterReceiver(
      fidl::InterfaceHandle<fuchsia::modular::MessageReader> receiver) override;

  // |fuchsia::modular::MessageQueue|
  void GetToken(GetTokenCallback callback) override;

  MessageQueueStorage* const queue_storage_;
};

// Class for managing a particular message queue, its tokens and its storage.
// Implementations of |fuchsia::modular::MessageQueue| and
// |fuchsia::modular::MessageSender| call into this class to manipulate the
// message queue. Owned by |MessageQueueManager|.
class MessageQueueStorage : fuchsia::modular::MessageSender {
 public:
  MessageQueueStorage(std::string queue_name, std::string queue_token,
                      const std::string& file_name_)
      : queue_name_(std::move(queue_name)),
        queue_token_(std::move(queue_token)),
        queue_data_(file_name_) {}

  ~MessageQueueStorage() override = default;

  void RegisterReceiver(
      fidl::InterfaceHandle<fuchsia::modular::MessageReader> receiver) {
    if (message_receiver_) {
      FXL_DLOG(WARNING)
          << "Existing fuchsia::modular::MessageReader is being replaced for "
             "message queue. queue name="
          << queue_name_;
    }

    message_receiver_.Bind(std::move(receiver));
    message_receiver_.set_error_handler(
        [this] {
          if (receive_ack_pending_) {
            FXL_DLOG(WARNING)
                << "MessageReceiver closed, but OnReceive acknowledgement still"
                   " pending.";
          }
          message_receiver_.Unbind();
          receive_ack_pending_ = false;
        });

    MaybeSendNextMessage();
  }

  const std::string& queue_token() const { return queue_token_; }

  void AddMessageSenderBinding(
      fidl::InterfaceRequest<fuchsia::modular::MessageSender> request) {
    message_sender_bindings_.AddBinding(this, std::move(request));
  }

  void AddMessageQueueBinding(
      fidl::InterfaceRequest<fuchsia::modular::MessageQueue> request) {
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

  // |fuchsia::modular::MessageSender|
  void Send(fidl::StringPtr message) override {
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
  fuchsia::modular::MessageReaderPtr message_receiver_;

  // When a |fuchsia::modular::MessageQueue| connection closes, the
  // corresponding MessageQueueConnection instance gets removed.
  fidl::BindingSet<fuchsia::modular::MessageQueue,
                   std::unique_ptr<MessageQueueConnection>>
      message_queue_bindings_;

  fidl::BindingSet<fuchsia::modular::MessageSender> message_sender_bindings_;
};

// MessageQueueConnection -----------------------------------------------------

MessageQueueConnection::MessageQueueConnection(
    MessageQueueStorage* const queue_storage)
    : queue_storage_(queue_storage) {}

MessageQueueConnection::~MessageQueueConnection() = default;

void MessageQueueConnection::RegisterReceiver(
    fidl::InterfaceHandle<fuchsia::modular::MessageReader> receiver) {
  queue_storage_->RegisterReceiver(std::move(receiver));
}

void MessageQueueConnection::GetToken(GetTokenCallback callback) {
  callback(queue_storage_->queue_token());
}

// MessageQueueManager --------------------------------------------------------

namespace {

std::string GenerateQueueToken() {
  // Get 256 bits of pseudo-randomness.
  constexpr size_t kBitCount = 256;
  constexpr size_t kBitsPerByte = 8;
  constexpr size_t kCharsPerByte = 2;
  constexpr size_t kByteCount = kBitCount / kBitsPerByte;
  constexpr char kHex[] = "0123456789ABCDEF";
  uint8_t bytes[kByteCount] = {};
  zx_cprng_draw(bytes, kByteCount);
  std::string token(kByteCount * kCharsPerByte, '\0');
  for (size_t i = 0; i < kByteCount; ++i) {
    uint8_t byte = bytes[i];
    token[2 * i] = kHex[byte & 0x0F];
    token[2 * i + 1] = kHex[byte / 0x10];
  }
  return token;
}

}  // namespace

class MessageQueueManager::GetQueueTokenCall
    : public PageOperation<fidl::StringPtr> {
 public:
  GetQueueTokenCall(fuchsia::ledger::Page* const page,
                    std::string component_namespace,
                    std::string component_instance_id,
                    const std::string& queue_name, ResultCall result_call)
      : PageOperation("MessageQueueManager::GetQueueTokenCall", page,
                      std::move(result_call), queue_name),
        component_namespace_(std::move(component_namespace)),
        component_instance_id_(std::move(component_instance_id)),
        queue_name_(queue_name) {}

 private:
  void Run() override {
    FlowToken flow{this, &result_};

    page()->GetSnapshot(snapshot_.NewRequest(),
                        fidl::VectorPtr<uint8_t>::New(0), nullptr,
                        Protect([this, flow](fuchsia::ledger::Status status) {
                          if (status != fuchsia::ledger::Status::OK) {
                            FXL_LOG(ERROR) << trace_name() << " "
                                           << "Page.GetSnapshot() " << status;
                            return;
                          }

                          Cont(flow);
                        }));
  }

  void Cont(FlowToken flow) {
    snapshot_.set_error_handler(
        [] { FXL_LOG(WARNING) << "Error on snapshot connection"; });

    key_ = MakeMessageQueueTokenKey(component_namespace_,
                                    component_instance_id_, queue_name_);
    snapshot_->Get(to_array(key_), [this, flow](fuchsia::ledger::Status status,
                                                fuchsia::mem::BufferPtr value) {
      if (status == fuchsia::ledger::Status::KEY_NOT_FOUND) {
        // Key wasn't found, that's not an error.
        return;
      }

      if (status != fuchsia::ledger::Status::OK) {
        FXL_LOG(ERROR) << trace_name() << " " << key_ << " "
                       << "PageSnapshot.Get() " << status;
        return;
      }

      if (!value) {
        FXL_LOG(ERROR) << trace_name() << " " << key_ << " "
                       << "Value is null.";
        return;
      }

      std::string queue_token;
      if (!fsl::StringFromVmo(*value, &queue_token)) {
        FXL_LOG(ERROR) << trace_name() << " " << key_ << " "
                       << "VMO could not be copied.";
        return;
      }
      result_ = queue_token;
    });
  }

  const std::string component_namespace_;
  const std::string component_instance_id_;
  const std::string queue_name_;
  fuchsia::ledger::PageSnapshotPtr snapshot_;
  std::string key_;

  fidl::StringPtr result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetQueueTokenCall);
};

class MessageQueueManager::GetMessageSenderCall : public PageOperation<> {
 public:
  GetMessageSenderCall(
      MessageQueueManager* const message_queue_manager,
      fuchsia::ledger::Page* const page, std::string token,
      fidl::InterfaceRequest<fuchsia::modular::MessageSender> request)
      : PageOperation("MessageQueueManager::GetMessageSenderCall", page, [] {}),
        message_queue_manager_(message_queue_manager),
        token_(std::move(token)),
        request_(std::move(request)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    page()->GetSnapshot(snapshot_.NewRequest(), nullptr, nullptr,
                        Protect([this, flow](fuchsia::ledger::Status status) {
                          if (status != fuchsia::ledger::Status::OK) {
                            FXL_LOG(ERROR) << trace_name() << " "
                                           << "Page.GetSnapshot() " << status;
                            return;
                          }

                          Cont(flow);
                        }));
  }

  void Cont(FlowToken flow) {
    std::string key = MakeMessageQueueKey(token_);
    snapshot_->Get(to_array(key), [this, flow](fuchsia::ledger::Status status,
                                               fuchsia::mem::BufferPtr value) {
      if (status != fuchsia::ledger::Status::OK) {
        if (status != fuchsia::ledger::Status::KEY_NOT_FOUND) {
          // It's expected that the key is not found when the link
          // is accessed for the first time. Don't log an error
          // then.
          FXL_LOG(ERROR) << trace_name() << " " << token_ << " "
                         << "PageSnapshot.Get() " << status;
        }
        return;
      }

      std::string value_as_string;
      if (value) {
        if (!fsl::StringFromVmo(*value, &value_as_string)) {
          FXL_LOG(ERROR) << trace_name() << " " << token_ << " "
                         << "VMO could not be copied.";
          return;
        }
      }

      if (!XdrRead(value_as_string, &result_, XdrMessageQueueInfo)) {
        return;
      }

      if (!result_.is_complete()) {
        FXL_LOG(WARNING) << trace_name() << " " << token_ << " "
                         << "Queue token not found in the ledger.";
        return;
      }

      message_queue_manager_->GetMessageQueueStorage(result_)
          ->AddMessageSenderBinding(std::move(request_));
    });
  }

  MessageQueueManager* const message_queue_manager_;  // not owned
  const std::string token_;
  fidl::InterfaceRequest<fuchsia::modular::MessageSender> request_;

  fuchsia::ledger::PageSnapshotPtr snapshot_;
  std::string key_;

  MessageQueueInfo result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetMessageSenderCall);
};

class MessageQueueManager::ObtainMessageQueueCall : public PageOperation<> {
 public:
  ObtainMessageQueueCall(
      MessageQueueManager* const message_queue_manager,
      fuchsia::ledger::Page* const page, const std::string& component_namespace,
      const std::string& component_instance_id, const std::string& queue_name,
      fidl::InterfaceRequest<fuchsia::modular::MessageQueue> request)
      : PageOperation("MessageQueueManager::ObtainMessageQueueCall", page,
                      [] {}, queue_name),
        message_queue_manager_(message_queue_manager),
        request_(std::move(request)) {
    message_queue_info_.component_namespace = component_namespace;
    message_queue_info_.component_instance_id = component_instance_id;
    message_queue_info_.queue_name = queue_name;
  }

 private:
  void Run() override {
    FlowToken flow{this};

    operation_collection_.Add(new GetQueueTokenCall(
        page(), message_queue_info_.component_namespace,
        message_queue_info_.component_instance_id,
        message_queue_info_.queue_name, [this, flow](fidl::StringPtr token) {
          if (token) {
            // Queue token was found in the ledger.
            message_queue_info_.queue_token = token.get();
            Finish(flow);
            return;
          }

          Cont(flow);
        }));
  }

  void Cont(FlowToken flow) {
    // Not found in the ledger, time to create a new message
    // queue.
    message_queue_info_.queue_token = GenerateQueueToken();

    page()->StartTransaction(Protect([this](fuchsia::ledger::Status status) {
      if (status != fuchsia::ledger::Status::OK) {
        FXL_LOG(ERROR) << trace_name() << " "
                       << "Page.StartTransaction() " << status;
      }
    }));

    const std::string message_queue_token_key =
        MakeMessageQueueTokenKey(message_queue_info_.component_namespace,
                                 message_queue_info_.component_instance_id,
                                 message_queue_info_.queue_name);

    page()->Put(to_array(message_queue_token_key),
                to_array(message_queue_info_.queue_token),
                Protect([this, key = message_queue_token_key](
                            fuchsia::ledger::Status status) {
                  if (status != fuchsia::ledger::Status::OK) {
                    FXL_LOG(ERROR) << trace_name() << " " << key << " "
                                   << "Page.Put() " << status;
                  }
                }));

    const std::string message_queue_key =
        MakeMessageQueueKey(message_queue_info_.queue_token);

    std::string json;
    XdrWrite(&json, &message_queue_info_, XdrMessageQueueInfo);

    page()->Put(to_array(message_queue_key), to_array(json),
                Protect([this, key = message_queue_key](
                            fuchsia::ledger::Status status) {
                  if (status != fuchsia::ledger::Status::OK) {
                    FXL_LOG(ERROR) << trace_name() << " " << key << " "
                                   << "Page.Put() " << status;
                  }
                }));

    page()->Commit(Protect([this, flow](fuchsia::ledger::Status status) {
      if (status != fuchsia::ledger::Status::OK) {
        FXL_LOG(ERROR) << trace_name() << " "
                       << "Page.Commit() " << status;
        return;
      }

      FXL_LOG(INFO) << trace_name() << " "
                    << "Created message queue: "
                    << message_queue_info_.queue_token;

      Finish(flow);
    }));
  }

  void Finish(FlowToken /*flow*/) {
    message_queue_manager_->GetMessageQueueStorage(message_queue_info_)
        ->AddMessageQueueBinding(std::move(request_));
  }

  MessageQueueManager* const message_queue_manager_;  // not owned
  fidl::InterfaceRequest<fuchsia::modular::MessageQueue> request_;

  MessageQueueInfo message_queue_info_;
  fuchsia::ledger::PageSnapshotPtr snapshot_;

  OperationCollection operation_collection_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ObtainMessageQueueCall);
};

class MessageQueueManager::DeleteMessageQueueCall : public PageOperation<> {
 public:
  DeleteMessageQueueCall(MessageQueueManager* const message_queue_manager,
                         fuchsia::ledger::Page* const page,
                         const std::string& component_namespace,
                         const std::string& component_instance_id,
                         const std::string& queue_name)
      : PageOperation("MessageQueueManager::DeleteMessageQueueCall", page,
                      [] {}, queue_name),
        message_queue_manager_(message_queue_manager) {
    message_queue_info_.component_namespace = component_namespace;
    message_queue_info_.component_instance_id = component_instance_id;
    message_queue_info_.queue_name = queue_name;
  }

 private:
  void Run() override {
    FlowToken flow{this};

    operation_collection_.Add(new GetQueueTokenCall(
        page(), message_queue_info_.component_namespace,
        message_queue_info_.component_instance_id,
        message_queue_info_.queue_name, [this, flow](fidl::StringPtr token) {
          if (!token) {
            FXL_LOG(WARNING)
                << trace_name() << " " << message_queue_info_.queue_name << " "
                << "Request to delete queue not found in ledger"
                << " for component instance "
                << message_queue_info_.component_instance_id << ".";
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
          page()->StartTransaction(
              Protect([this](fuchsia::ledger::Status status) {
                if (status != fuchsia::ledger::Status::OK) {
                  FXL_LOG(ERROR) << trace_name() << " "
                                 << "Page.StartTransaction() " << status;
                }
              }));

          page()->Delete(to_array(message_queue_key),
                         Protect([this, key = message_queue_key](
                                     fuchsia::ledger::Status status) {
                           if (status != fuchsia::ledger::Status::OK) {
                             FXL_LOG(ERROR) << trace_name() << " " << key << " "
                                            << "Page.Delete() " << status;
                           }
                         }));

          page()->Delete(to_array(message_queue_token_key),
                         Protect([this, key = message_queue_token_key](
                                     fuchsia::ledger::Status status) {
                           if (status != fuchsia::ledger::Status::OK) {
                             FXL_LOG(ERROR) << trace_name() << " " << key << " "
                                            << "Page.Delete() " << status;
                           }
                         }));

          message_queue_manager_->ClearMessageQueueStorage(message_queue_info_);

          page()->Commit(Protect([this, flow](fuchsia::ledger::Status status) {
            if (status != fuchsia::ledger::Status::OK) {
              FXL_LOG(ERROR) << trace_name() << " "
                             << "Page.Commit() " << status;
              return;
            }

            FXL_LOG(INFO) << trace_name() << " "
                          << "Deleted message queue: "
                          << message_queue_info_.component_instance_id << "/"
                          << message_queue_info_.queue_name;
          }));
        }));
  }

  MessageQueueManager* const message_queue_manager_;  // not owned
  MessageQueueInfo message_queue_info_;
  fuchsia::ledger::PageSnapshotPtr snapshot_;

  OperationCollection operation_collection_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteMessageQueueCall);
};

class MessageQueueManager::DeleteNamespaceCall : public PageOperation<> {
 public:
  DeleteNamespaceCall(MessageQueueManager* const message_queue_manager,
                      fuchsia::ledger::Page* const page,
                      const std::string& component_namespace,
                      std::function<void()> done)
      : PageOperation("MessageQueueManager::DeleteNamespaceCall", page,
                      std::move(done), component_namespace),
        message_queue_manager_(message_queue_manager),
        component_namespace_(component_namespace),
        message_queues_key_prefix_(
            MakeMessageQueuesPrefix(component_namespace)) {}

 private:
  void Run() override {
    FlowToken flow{this};
    page()->GetSnapshot(snapshot_.NewRequest(),
                        to_array(message_queues_key_prefix_), nullptr,
                        Protect([this, flow](fuchsia::ledger::Status status) {
                          if (status != fuchsia::ledger::Status::OK) {
                            FXL_LOG(ERROR) << trace_name() << " "
                                           << "Page.GetSnapshot() " << status;
                            return;
                          }
                          GetKeysToDelete(flow);
                        }));
  }

  void GetKeysToDelete(FlowToken flow) {
    GetEntries(snapshot_.get(), &component_entries_,
               [this, flow](fuchsia::ledger::Status status) {
                 if (status != fuchsia::ledger::Status::OK) {
                   FXL_LOG(ERROR) << trace_name() << " "
                                  << "GetEntries() " << status;
                   return;
                 }

                 ProcessKeysToDelete(flow);
               });
  }

  void ProcessKeysToDelete(FlowToken flow) {
    std::vector<std::string> keys_to_delete;
    for (const auto& entry : component_entries_) {
      FXL_DCHECK(entry.value) << "Value vmo handle is null";

      keys_to_delete.push_back(to_string(entry.key));

      std::string queue_token;
      if (!fsl::StringFromVmo(*entry.value, &queue_token)) {
        FXL_LOG(ERROR) << trace_name() << " " << to_string(entry.key)
                       << "VMO could not be copied.";
        continue;
      }

      keys_to_delete.push_back(MakeMessageQueueKey(queue_token));
    }

    for (auto& i : keys_to_delete) {
      page()->Delete(
          to_array(i), Protect([this, i, flow](fuchsia::ledger::Status status) {
            if (status != fuchsia::ledger::Status::OK) {
              FXL_LOG(ERROR)
                  << trace_name() << " " << i << "Page.Delete() " << status;
            }
          }));
    }

    message_queue_manager_->ClearMessageQueueStorage(component_namespace_);
  }

  MessageQueueManager* const message_queue_manager_;  // not owned
  fuchsia::ledger::PageSnapshotPtr snapshot_;
  const std::string component_namespace_;
  const std::string message_queues_key_prefix_;
  std::vector<fuchsia::ledger::Entry> component_entries_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteNamespaceCall);
};

MessageQueueManager::MessageQueueManager(LedgerClient* const ledger_client,
                                         fuchsia::ledger::PageId page_id,
                                         std::string local_path)
    : PageClient("MessageQueueManager", ledger_client, std::move(page_id)),
      local_path_(std::move(local_path)) {}

MessageQueueManager::~MessageQueueManager() = default;

void MessageQueueManager::ObtainMessageQueue(
    const std::string& component_namespace,
    const std::string& component_instance_id, const std::string& queue_name,
    fidl::InterfaceRequest<fuchsia::modular::MessageQueue> request) {
  operation_collection_.Add(new ObtainMessageQueueCall(
      this, page(), component_namespace, component_instance_id, queue_name,
      std::move(request)));
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
    ComponentQueueNameMap<ValueType>& queue_map, const MessageQueueInfo& info) {
  auto it1 = queue_map.find(info.component_namespace);
  if (it1 != queue_map.end()) {
    auto it2 = it1->second.find(info.component_instance_id);
    if (it2 != it1->second.end()) {
      it2->second.erase(info.queue_name);
    }
  }
}

template <typename ValueType>
void MessageQueueManager::EraseNamespace(
    ComponentQueueNameMap<ValueType>& queue_map,
    const std::string& component_namespace) {
  auto it1 = queue_map.find(component_namespace);
  if (it1 != queue_map.end()) {
    it1->second.erase(component_namespace);
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

    message_queue_infos_[info.queue_token] = info;

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
  // close all outstanding fuchsia::modular::MessageSender and
  // fuchsia::modular::MessageQueue interface connections, and delete all
  // messages on the queue permanently.
  message_queues_.erase(info.queue_token);

  // Clear entries in message_queue_tokens_ and
  // pending_watcher_callbacks_.
  EraseQueueName(pending_watcher_callbacks_, info);
  EraseQueueName(message_queue_tokens_, info);

  auto deletion_watchers = FindQueueName(deletion_watchers_, info);
  for (const auto& component_namespace_iter : *deletion_watchers) {
    for (const auto& component_instance_iter :
         component_namespace_iter.second) {
      component_instance_iter.second();
    }
  }

  EraseQueueName(deletion_watchers_, info);
}

void MessageQueueManager::ClearMessageQueueStorage(
    const std::string& component_namespace) {
  auto namespace_to_delete = deletion_watchers_[component_namespace];
  for (const auto& instances_in_namespace : namespace_to_delete) {
    for (const auto& queue_names : instances_in_namespace.second) {
      for (const auto& watcher_namespace : queue_names.second) {
        for (const auto& watcher_instance : watcher_namespace.second) {
          watcher_instance.second();
        }
      }
    }
  }

  EraseNamespace(pending_watcher_callbacks_, component_namespace);
  EraseNamespace(message_queue_tokens_, component_namespace);
  EraseNamespace(deletion_watchers_, component_namespace);
}

void MessageQueueManager::DeleteMessageQueue(
    const std::string& component_namespace,
    const std::string& component_instance_id, const std::string& queue_name) {
  operation_collection_.Add(new DeleteMessageQueueCall(
      this, page(), component_namespace, component_instance_id, queue_name));
}

void MessageQueueManager::DeleteNamespace(
    const std::string& component_namespace, std::function<void()> done) {
  operation_collection_.Add(new DeleteNamespaceCall(
      this, page(), component_namespace, std::move(done)));
}

void MessageQueueManager::GetMessageSender(
    const std::string& queue_token,
    fidl::InterfaceRequest<fuchsia::modular::MessageSender> request) {
  const auto& it = message_queues_.find(queue_token);
  if (it != message_queues_.cend()) {
    // Found the message queue already.
    it->second->AddMessageSenderBinding(std::move(request));
    return;
  }

  operation_collection_.Add(
      new GetMessageSenderCall(this, page(), queue_token, std::move(request)));
}

void MessageQueueManager::RegisterMessageWatcher(
    const std::string& component_namespace,
    const std::string& component_instance_id, const std::string& queue_name,
    const std::function<void()>& watcher) {
  const std::string* const token =
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

void MessageQueueManager::RegisterDeletionWatcher(
    const std::string& component_namespace,
    const std::string& component_instance_id, const std::string& queue_token,
    const std::function<void()>& watcher) {
  auto it = message_queue_infos_.find(queue_token);
  if (it == message_queue_infos_.end()) {
    return;
  }

  const MessageQueueInfo queue_info = it->second;

  deletion_watchers_[queue_info.component_namespace]
                    [queue_info.component_instance_id][queue_info.queue_name]
                    [component_namespace][component_instance_id] = watcher;
}

void MessageQueueManager::DropMessageWatcher(
    const std::string& component_namespace,
    const std::string& component_instance_id, const std::string& queue_name) {
  MessageQueueInfo queue_info{component_namespace, component_instance_id,
                              queue_name, ""};
  const std::string* const token =
      FindQueueName(message_queue_tokens_, queue_info);
  if (token) {
    // The |MessageQueueStorage| doesn't exist yet.
    EraseQueueName(message_queue_tokens_, queue_info);
    return;
  }

  auto msq_it = message_queues_.find(*token);
  if (msq_it == message_queues_.end()) {
    FXL_LOG(WARNING) << "Asked to DropWatcher for a queue that doesn't exist";
    return;
  }
  msq_it->second->DropWatcher();
}

void MessageQueueManager::DropDeletionWatcher(
    const std::string& watcher_namespace,
    const std::string& watcher_instance_id, const std::string& queue_token) {
  auto it = message_queue_infos_.find(queue_token);
  if (it == message_queue_infos_.end()) {
    return;
  }

  const MessageQueueInfo queue_info = it->second;
  deletion_watchers_[queue_info.component_namespace]
                    [queue_info.component_instance_id][queue_info.queue_name]
                    [watcher_namespace]
                        .erase(watcher_instance_id);
}

}  // namespace modular
