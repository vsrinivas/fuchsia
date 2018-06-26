// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/agents/clipboard/clipboard_storage.h"

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"

namespace modular {
namespace {

fidl::VectorPtr<uint8_t> ToArray(const std::string& str) {
  auto array = fidl::VectorPtr<uint8_t>::New(str.size());
  memcpy(array->data(), str.data(), str.size());
  return array;
}

std::string ToString(fuchsia::mem::Buffer value) {
  fsl::SizedVmo vmo;
  std::string parsed_string;
  if (!fsl::SizedVmo::FromTransport(std::move(value), &vmo)) {
    FXL_LOG(ERROR) << "Could not decode clipboard value.";
    return "";
  }
  if (!fsl::StringFromVmo(vmo, &parsed_string)) {
    FXL_LOG(ERROR)
        << "fuchsia::modular::Clipboard vmo could not be decoded to string.";
    return "";
  }
  return parsed_string;
}

// The Ledger key that is used to store the current value.
constexpr char kCurrentValueKey[] = "current_value";

}  // namespace

class ClipboardStorage::PushCall : public Operation<> {
 public:
  PushCall(ClipboardStorage* const impl, const fidl::StringPtr& text)
      : Operation("ClipboardStorage::PushCall", [] {}),
        impl_(impl),
        text_(text) {}

 private:
  void Run() override {
    FlowToken flow{this};
    impl_->page()->Put(ToArray(kCurrentValueKey), ToArray(text_),
                       [this, flow](fuchsia::ledger::Status status) {
                         if (status != fuchsia::ledger::Status::OK) {
                           FXL_LOG(ERROR) << "Failed to put text: " << text_;
                         }
                       });
  }

  ClipboardStorage* const impl_;  // not owned
  const fidl::StringPtr text_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PushCall);
};

class ClipboardStorage::PeekCall : public Operation<fidl::StringPtr> {
 public:
  PeekCall(ClipboardStorage* const impl,
           std::function<void(fidl::StringPtr)> result)
      : Operation("ClipboardStorage::PeekCall", std::move(result)),
        impl_(impl) {
    // No error checking: Absent ledger value yields "", not
    // null. TODO(mesch): Once we support types, distinction of
    // null may make sense.
    text_.reset("");
  }

 private:
  void Run() override {
    FlowToken flow{this, &text_};
    impl_->page()->GetSnapshot(
        snapshot_.NewRequest(), fidl::VectorPtr<uint8_t>::New(0), nullptr,
        [](fuchsia::ledger::Status status) {
          if (status != fuchsia::ledger::Status::OK) {
            FXL_LOG(ERROR) << "Failed to get page snapshot";
          }
        });

    snapshot_->Get(ToArray(kCurrentValueKey),
                   [this, flow](fuchsia::ledger::Status status,
                                fuchsia::mem::BufferPtr value) {
                     if (value) {
                       text_ = ToString(std::move(*value));
                     }
                   });
  }

  ClipboardStorage* const impl_;  // not owned
  fuchsia::ledger::PageSnapshotPtr snapshot_;
  fidl::StringPtr text_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PeekCall);
};

ClipboardStorage::ClipboardStorage(LedgerClient* ledger_client,
                                   LedgerPageId page_id)
    : PageClient("ClipboardStorage", ledger_client, std::move(page_id)) {}

ClipboardStorage::~ClipboardStorage() = default;

void ClipboardStorage::Push(const fidl::StringPtr& text) {
  operation_queue_.Add(new PushCall(this, text));
}

void ClipboardStorage::Peek(
    const std::function<void(fidl::StringPtr)>& callback) {
  operation_queue_.Add(new PeekCall(this, callback));
}

}  // namespace modular
