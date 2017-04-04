// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/component/message_queue_manager.h"

#include <algorithm>
#include <deque>

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/services/component/message_queue.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

namespace {

// All message queue information is stored in one page.
// That page has keys mapping component instance ids and queue names to queue
// tokens (in the form "CQ|component_instance_id|queue_name"), keys mapping
// queue tokens to component instance ids (in the form "T|queue_token|C") and
// keys mapping queue tokens to queue names (in the form  "T|queue_token|Q").
// The component instance id and queue name for a queue token can be retrieved
// in a single operation with a prefix query for "T|queue_token|".

constexpr char kComponentQueuePrefix[] = "CQ";
constexpr char kTokenPrefix[] = "T";
constexpr char kComponentSuffix[] = "C";
constexpr char kQueueSuffix[] = "Q";
constexpr uint8_t kSeparator = '|';

void AppendEscaped(std::string* key, const std::string& data) {
  for (uint8_t c : data) {
    if (c == kSeparator) {
      // If the separator exists in the string, send it twice.
      key->push_back(kSeparator);
    }
    key->push_back(c);
  }
}

void AppendSeparator(std::string* const key) {
  key->push_back(kSeparator);
}

std::string MakeQueueTokenKey(const std::string& component_instance_id,
                              const std::string& queue_name) {
  std::string key;
  AppendEscaped(&key, kComponentQueuePrefix);
  AppendSeparator(&key);
  AppendEscaped(&key, component_instance_id);
  AppendSeparator(&key);
  AppendEscaped(&key, queue_name);
  return key;
}

std::string MakeQueueTokenPrefix(const std::string& queue_token) {
  std::string key;
  AppendEscaped(&key, kTokenPrefix);
  AppendSeparator(&key);
  AppendEscaped(&key, queue_token);
  AppendSeparator(&key);
  return key;
}

std::string MakeComponentInstanceIdKey(const std::string& queue_token) {
  std::string key(MakeQueueTokenPrefix(queue_token));
  AppendEscaped(&key, kComponentSuffix);
  return key;
}

std::string MakeQueueNameKey(const std::string& queue_token) {
  std::string key(MakeQueueTokenPrefix(queue_token));
  AppendEscaped(&key, kQueueSuffix);
  return key;
}

}  // namespace

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
  void Receive(const MessageQueue::ReceiveCallback& callback) override;

  // |MessageQueue|
  void GetToken(const GetTokenCallback& callback) override;

  MessageQueueStorage* const queue_storage_;
};

// Class for managing a particular message queue, its tokens and its storage.
// Implementations of |MessageQueue| and |MessageSender| call into this class to
// manipulate the message queue. Owned by |MessageQueueManager|.
class MessageQueueStorage : MessageSender {
 public:
  MessageQueueStorage(const std::string& queue_token)
      : queue_token_(queue_token) {}

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

  const std::string& queue_token() const { return queue_token_; }

  void AddMessageSenderBinding(fidl::InterfaceRequest<MessageSender> request) {
    message_sender_bindings_.AddBinding(this, std::move(request));
  }

  void AddMessageQueueBinding(fidl::InterfaceRequest<MessageQueue> request) {
    message_queue_bindings_.AddBinding(
        std::make_unique<MessageQueueConnection>(this), std::move(request));
  }

  // |MessageQueueConnection| calls this method in its destructor so that we can
  // drop any pending receive callbacks.
  void RemoveMessageQueueConnection(const MessageQueueConnection* const conn) {
    auto i = std::remove_if(receive_callback_queue_.begin(),
                            receive_callback_queue_.end(),
                            [conn](const ReceiveCallbackQueueItem& item) {
                              return conn == item.first;
                            });
    receive_callback_queue_.erase(i, receive_callback_queue_.end());
  }

  void RegisterWatcher(const std::function<void()>& watcher) {
    watcher_ = watcher;
    if (watcher_) {
      watcher_();
    }
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

  const std::string queue_token_;

  std::function<void()> watcher_;

  std::deque<std::string> queue_data_;

  using ReceiveCallbackQueueItem =
      std::pair<const MessageQueueConnection*, MessageQueue::ReceiveCallback>;
  std::deque<ReceiveCallbackQueueItem> receive_callback_queue_;

  // When a |MessageQueue| connection closes, the corresponding
  // MessageQueueConnection instance gets removed (and destroyed due
  // to unique_ptr semantics), which in turn will notify our
  // RemoveMessageQueueConnection method.
  fidl::BindingSet<MessageQueue, std::unique_ptr<MessageQueueConnection>>
      message_queue_bindings_;

  fidl::BindingSet<MessageSender> message_sender_bindings_;
};

// MessageQueueConnection -----------------------------------------------------

MessageQueueConnection::MessageQueueConnection(
    MessageQueueStorage* const queue_storage)
    : queue_storage_(queue_storage) {}

MessageQueueConnection::~MessageQueueConnection() {
  queue_storage_->RemoveMessageQueueConnection(this);
}

void MessageQueueConnection::Receive(
    const MessageQueue::ReceiveCallback& callback) {
  queue_storage_->Receive(this, callback);
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
  mx_cprng_draw(&randomness, sizeof randomness, &random_size);
  // TODO: is there a more efficient way to do this?
  std::string token;
  for (uint8_t byte : randomness) {
    ftl::StringAppendf(&token, "%X", byte);
  }
  return token;
}

}  // namespace

class MessageQueueManager::GetQueueTokenCall : Operation<fidl::String> {
 public:
  GetQueueTokenCall(OperationContainer* const container,
                    ledger::Page* const page,
                    const std::string& component_instance_id,
                    const std::string& queue_name,
                    ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_(page),
        component_instance_id_(component_instance_id),
        queue_name_(queue_name) {
    Ready();
  }

 private:
  void Run() override {
    page_->GetSnapshot(
        snapshot_.NewRequest(), nullptr,
        [this](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "Ledger.GetSnapshot() " << status;
            Done(std::move(nullptr));
            return;
          }

          snapshot_.set_connection_error_handler(
              [] { FTL_LOG(WARNING) << "Error on snapshot connection"; });

          key_ = MakeQueueTokenKey(component_instance_id_, queue_name_);
          snapshot_->Get(to_array(key_), [this] (ledger::Status status, mx::vmo value) {
              if (status == ledger::Status::KEY_NOT_FOUND) {
                // Key wasn't found, that's not an error.
                Done(nullptr);
                return;
              }

              if (status != ledger::Status::OK) {
                FTL_LOG(ERROR) << "Failed to get key " << key_;
                Done(nullptr);
                return;
              }

              if (!value) {
                FTL_LOG(ERROR) << "Key " << key_ << " has no value";
                Done(nullptr);
                return;
              }

              std::string queue_token;
              if (!mtl::StringFromVmo(value, &queue_token)) {
                FTL_LOG(ERROR) << "VMO for key " << key_
                               << " couldn't be copied.";
                Done(nullptr);
                return;
              }

              Done(queue_token);
            });
        });
  }

  ledger::Page* const page_;  // not owned
  const std::string component_instance_id_;
  const std::string queue_name_;
  ledger::PageSnapshotPtr snapshot_;
  std::string key_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetQueueTokenCall);
};

struct ResolveTokenResult {
  std::string component_instance_id;
  std::string queue_name;

  bool complete() const {
    return !component_instance_id.empty() &&
        !queue_name.empty();
  }
};

class MessageQueueManager::ResolveTokenCall : Operation<ResolveTokenResult> {
 public:
  ResolveTokenCall(OperationContainer* const container,
                   ledger::Page* const page,
                   const std::string& token,
                   ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_(page),
        token_(token) {
    Ready();
  }

 private:
  void Run() override {
    page_->GetSnapshot(
        snapshot_.NewRequest(), nullptr, [this](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "Failed to get snapshot for page";
            Done(std::move(result_));
            return;
          }

          key_ = MakeQueueTokenPrefix(token_);
          snapshot_->GetEntries(
              to_array(key_), nullptr,
              [this](ledger::Status status, fidl::Array<ledger::EntryPtr> entries,
                     fidl::Array<uint8_t> continuation_token) {
                if (status != ledger::Status::OK) {
                  FTL_LOG(ERROR)
                      << "Got ledger status " << status
                      << " when requesting token prefix " << key_;
                  Done(std::move(result_));
                  return;
                }

                if (entries.size() == 0) {
                  // Keys weren't found, that's not an error.
                  Done(std::move(result_));
                  return;
                }

                if (entries.size() != 2) {
                  FTL_LOG(ERROR) << "Expected 2 entries with prefix "
                                 << key_ << " got " << entries.size();
                  Done(std::move(result_));
                  return;
                }

                std::string component_key{MakeComponentInstanceIdKey(token_)};
                std::string queue_key{MakeQueueNameKey(token_)};
                for (const auto& i : entries) {
                  const ftl::StringView key_string(
                      reinterpret_cast<const char*>(i->key.data()),
                      i->key.size());

                  if (!i->value) {
                    FTL_LOG(ERROR) << "Key " << key_string << " has no value";
                    Done(std::move(result_));
                    return;
                  }

                  std::string value_string;
                  if (!mtl::StringFromVmo(i->value, &value_string)) {
                    FTL_LOG(ERROR)
                        << "VMO for key " << key_string << " couldn't be copied.";
                    Done(std::move(result_));
                    return;
                  }

                  if (key_string == component_key) {
                    result_.component_instance_id = std::move(value_string);
                  } else if (key_string == queue_key) {
                    result_.queue_name = std::move(value_string);
                  } else {
                    FTL_LOG(ERROR) << "Unexpected key returned " << key_string;
                    Done(std::move(result_));
                    return;
                  }
                }

                Done(std::move(result_));
              });
        });
  }

  ledger::Page* const page_;  // not owned
  const std::string token_;

  ledger::PageSnapshotPtr snapshot_;
  std::string key_;

  ResolveTokenResult result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ResolveTokenCall);
};

class MessageQueueManager::GetMessageQueueStorageCall : Operation<MessageQueueStorage*> {
 public:
  GetMessageQueueStorageCall(
      OperationContainer* const container,
      MessageQueueManager* const message_queue_manager,
      ledger::Page* const page,
      const std::string& component_instance_id,
      const std::string& queue_name,
      ResultCall result_call)
      : Operation(container, std::move(result_call)),
        message_queue_manager_(message_queue_manager),
        page_(page),
        component_instance_id_(component_instance_id),
        queue_name_(queue_name) {
    Ready();
  }

 private:
  void Run() override {
    new GetQueueTokenCall(
        &operation_collection_,
        page_,
        component_instance_id_,
        queue_name_,
        [this](fidl::String token) {
          if (token) {
            // Queue token was found in the ledger.
            Done(message_queue_manager_->GetMessageQueueStorage(
                component_instance_id_, queue_name_, token.get()));
            return;
          }

          // Not found in the ledger, time to create a new message queue.
          token_ = GenerateQueueToken();

          page_->StartTransaction([](ledger::Status status) {});

          page_->Put(
              to_array(MakeQueueTokenKey(component_instance_id_, queue_name_)),
              to_array(token_), [](ledger::Status status) {});

          page_->Put(to_array(MakeComponentInstanceIdKey(token_)),
                     to_array(component_instance_id_),
                     [](ledger::Status status) {});

          page_->Put(to_array(MakeQueueNameKey(token_)),
                     to_array(queue_name_), [](ledger::Status status) {});

          page_->Commit([this](ledger::Status status) {
              if (status != ledger::Status::OK) {
                FTL_LOG(ERROR) << "Error creating queue in ledger: " << status;
                Done(nullptr);
                return;
              }
              FTL_LOG(INFO) << "Created queue in ledger: " << token_;
              Done(message_queue_manager_->GetMessageQueueStorage(
                  component_instance_id_, queue_name_, token_));
            });
        });
  }

  MessageQueueManager* const message_queue_manager_;  // not owned
  ledger::Page* const page_;  // not owned
  const std::string component_instance_id_;
  const std::string queue_name_;
  ledger::PageSnapshotPtr snapshot_;
  std::string token_;

  OperationCollection operation_collection_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetMessageQueueStorageCall);
};

class MessageQueueManager::DeleteMessageQueueCall : Operation<void> {
 public:
  DeleteMessageQueueCall(OperationContainer* const container,
                         MessageQueueManager* const message_queue_manager,
                         ledger::Page* const page,
                         const std::string& component_instance_id,
                         const std::string& queue_name)
      : Operation(container, []{}),
        message_queue_manager_(message_queue_manager),
        page_(page),
        component_instance_id_(component_instance_id),
        queue_name_(queue_name) {
    Ready();
  }

 private:
  void Run() override {
    new GetQueueTokenCall(
        &operation_collection_,
        page_,
        component_instance_id_,
        queue_name_,
        [this](fidl::String token) {
          if (!token) {
            FTL_LOG(WARNING) << "Request to delete queue " << queue_name_
                             << " for component instance "
                             << component_instance_id_
                             << " that wasn't found in the ledger";
            Done();
            return;
          }

          token_ = token.get();

          // Delete the ledger entries.
          page_->StartTransaction([](ledger::Status status) {});
          page_->Delete(
              to_array(MakeQueueTokenKey(component_instance_id_, queue_name_)),
              [](ledger::Status status) {});
          page_->Delete(to_array(MakeComponentInstanceIdKey(token_)),
                        [](ledger::Status status) {});
          page_->Delete(to_array(MakeQueueNameKey(token_)),
                        [](ledger::Status status) {});

          message_queue_manager_->ClearMessageQueueStorage(
              component_instance_id_, queue_name_, token_);

          page_->Commit([this](ledger::Status status) {
              if (status == ledger::Status::OK) {
                FTL_LOG(INFO) << "Deleted queue from ledger: " << token_;
              } else {
                FTL_LOG(ERROR) << "Error deleting queue from ledger: " << status;
              }
              Done();
            });
        });
  }

  MessageQueueManager* const message_queue_manager_;  // not owned
  ledger::Page* const page_;  // not owned
  const std::string component_instance_id_;
  const std::string queue_name_;
  ledger::PageSnapshotPtr snapshot_;
  std::string token_;

  OperationCollection operation_collection_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeleteMessageQueueCall);
};

MessageQueueManager::MessageQueueManager(ledger::PagePtr page)
    : page_(std::move(page)) {}

MessageQueueManager::~MessageQueueManager() = default;

void MessageQueueManager::ObtainMessageQueue(
    const std::string& component_instance_id,
    const std::string& queue_name,
    fidl::InterfaceRequest<MessageQueue> request) {
  new GetMessageQueueStorageCall(
      &operation_collection_,
      this,
      page_.get(),
      component_instance_id,
      queue_name,
      ftl::MakeCopyable([request = std::move(request)](
          MessageQueueStorage* const mqs) mutable {
                          if (mqs) {
                            mqs->AddMessageQueueBinding(std::move(request));
                          }
                        }));
}

MessageQueueStorage* MessageQueueManager::GetMessageQueueStorage(
    const std::string& component_instance_id,
    const std::string& queue_name,
    const std::string& queue_token) {
  auto it = message_queues_.find(queue_token);
  if (it == message_queues_.end()) {
    // Not found, create one.
    bool inserted;
    auto new_queue = std::make_unique<MessageQueueStorage>(queue_token);
    std::tie(it, inserted) = message_queues_.insert(
        std::make_pair(queue_token, std::move(new_queue)));
    FTL_DCHECK(inserted);

    auto queue_pair = std::make_pair(component_instance_id, queue_name);
    message_queue_tokens_[queue_pair] = queue_token;
    auto watcher_it = pending_watcher_callbacks_.find(queue_pair);
    if (watcher_it != pending_watcher_callbacks_.end()) {
      it->second.get()->RegisterWatcher(watcher_it->second);
      pending_watcher_callbacks_.erase(watcher_it);
    }
  }
  return it->second.get();
}

void MessageQueueManager::ClearMessageQueueStorage(
    const std::string& component_instance_id,
    const std::string& queue_name,
    const std::string& queue_token) {
  // Remove the |MessageQueueStorage| and delete it which in turn will
  // close all outstanding MessageSender and MessageQueue interface
  // connections, and delete all messages on the queue permanently.
  message_queues_.erase(queue_token);

  // Clear entries in message_queue_tokens_ and
  // pending_watcher_callbacks_.
  auto queue_pair = std::make_pair(component_instance_id, queue_name);
  message_queue_tokens_.erase(queue_pair);
  pending_watcher_callbacks_.erase(queue_pair);
}

void MessageQueueManager::DeleteMessageQueue(
    const std::string& component_instance_id,
    const std::string& queue_name) {
  new DeleteMessageQueueCall(&operation_collection_,
                             this,
                             page_.get(),
                             component_instance_id,
                             queue_name);
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

  new ResolveTokenCall(
      &operation_collection_,
      page_.get(),
      queue_token,
      ftl::MakeCopyable([ this, queue_token, request = std::move(request) ](
          ResolveTokenResult result) mutable {
                          if (!result.complete()) {
                            FTL_LOG(WARNING) << "Queue token " << queue_token
                                             << " not found in the ledger.";
                            return;
                          }
                          GetMessageQueueStorage(
                              result.component_instance_id,
                              result.queue_name,
                              queue_token)
                              ->AddMessageSenderBinding(std::move(request));
                        }));
}

void MessageQueueManager::RegisterWatcher(
    const std::string& component_instance_id,
    const std::string& queue_name,
    const std::function<void()>& watcher) {
  auto queue_pair = std::make_pair(component_instance_id, queue_name);

  auto token_it = message_queue_tokens_.find(queue_pair);
  if (token_it == message_queue_tokens_.end()) {
    // The |MessageQueueStorage| doesn't exist yet. Stash the watcher.
    pending_watcher_callbacks_[queue_pair] = watcher;
    return;
  }

  auto msq_it = message_queues_.find(token_it->second);
  FTL_DCHECK(msq_it != message_queues_.end());
  msq_it->second->RegisterWatcher(watcher);
}

void MessageQueueManager::DropWatcher(const std::string& component_instance_id,
                                      const std::string& queue_name) {
  auto queue_pair = std::make_pair(component_instance_id, queue_name);

  auto token_it = message_queue_tokens_.find(queue_pair);
  if (token_it == message_queue_tokens_.end()) {
    // The |MessageQueueStorage| doesn't exist yet.
    pending_watcher_callbacks_.erase(queue_pair);
    return;
  }

  auto msq_it = message_queues_.find(token_it->second);
  if (msq_it == message_queues_.end()) {
    FTL_LOG(WARNING) << "Asked to DropWatcher for a queue that doesn't exist";
    return;
  };
  msq_it->second->DropWatcher();
}

std::size_t MessageQueueManager::StringPairHash::operator()(
    const std::pair<std::string, std::string>& p) const {
  std::string s;
  s.append(p.first);
  s.push_back('\0');
  s.append(p.second);
  return std::hash<std::string>{}(s);
}

}  // namespace modular
