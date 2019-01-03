// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>

#include "peridot/lib/fidl/array_to_string.h"  // for to_string(), to_array()

namespace modular {

// fit::promise wrapper functions for fuchsia.ledger.Page.
//
// These methods match the signatures in fuchsia.ledger.Page with the exception
// that the first parameter is always a fuchsia::ledger::Page*.
class PagePromise {
 public:
  static fit::promise<> StartTransaction(fuchsia::ledger::Page* page) {
    fit::bridge<> bridge;
    page->StartTransaction([completer = std::move(bridge.completer)](
                               fuchsia::ledger::Status status) mutable {
      if (status == fuchsia::ledger::Status::OK) {
        completer.complete_ok();
      } else {
        completer.complete_error();
      }
    });
    return bridge.consumer.promise();
  }

  static fit::promise<> Commit(fuchsia::ledger::Page* page) {
    fit::bridge<> bridge;
    page->Commit([completer = std::move(bridge.completer)](
                     fuchsia::ledger::Status status) mutable {
      if (status == fuchsia::ledger::Status::OK) {
        completer.complete_ok();
      } else {
        completer.complete_error();
      }
    });
    return bridge.consumer.promise();
  }

  static fit::promise<> Rollback(fuchsia::ledger::Page* page) {
    fit::bridge<> bridge;
    page->Commit([completer = std::move(bridge.completer)](
                     fuchsia::ledger::Status status) mutable {
      if (status == fuchsia::ledger::Status::OK) {
        completer.complete_ok();
      } else {
        completer.complete_error();
      }
    });
    return bridge.consumer.promise();
  }

  static fit::promise<> GetSnapshot(
      fuchsia::ledger::Page* page,
      fidl::InterfaceRequest<fuchsia::ledger::PageSnapshot> request) {
    fit::bridge<> bridge;
    page->GetSnapshot(std::move(request),
                      fidl::VectorPtr<uint8_t>::New(0) /* key_prefix */,
                      nullptr /* watcher */,
                      [completer = std::move(bridge.completer)](
                          fuchsia::ledger::Status status) mutable {
                        if (status == fuchsia::ledger::Status::OK) {
                          completer.complete_ok();
                        } else {
                          completer.complete_error();
                        }
                      });
    return bridge.consumer.promise();
  }

  static fit::promise<> Put(fuchsia::ledger::Page* page, std::string key,
                            std::vector<uint8_t> value) {
    fit::bridge<> bridge;
    page->Put(to_array(key), fidl::VectorPtr<uint8_t>(std::move(value)),
              [completer = std::move(bridge.completer)](
                  fuchsia::ledger::Status status) mutable {
                if (status == fuchsia::ledger::Status::OK) {
                  completer.complete_ok();
                } else {
                  completer.complete_error();
                }
              });
    return bridge.consumer.promise();
  }
};

// fit::promise wrapper functions for fuchsia.ledger.Page.
//
// These methods match the signatures in fuchsia.ledger.Page with the exception
// that the first parameter is always a fuchsia::ledger::Page*.
class PageSnapshotPromise {
 public:
  // fit::promise wrapper function for PageSnapshot.GetInline().
  //
  // Falls back to PageSnapshot::Get() if the value is too large.
  // TODO(thatguy): Implement the fallback.
  static fit::promise<std::unique_ptr<std::vector<uint8_t>>> GetInline(
      fuchsia::ledger::PageSnapshot* snapshot, std::string key) {
    fit::bridge<std::unique_ptr<std::vector<uint8_t>>> bridge;
    snapshot->GetInline(
        to_array(key), [completer = std::move(bridge.completer)](
                           fuchsia::ledger::Status status,
                           fuchsia::ledger::InlinedValuePtr value) mutable {
          switch (status) {
            case fuchsia::ledger::Status::OK:
            case fuchsia::ledger::Status::KEY_NOT_FOUND:
              if (value) {
                // Convert the result to a unique_ptr instead of a VectorPtr.
                auto ret =
                    std::make_unique<std::vector<uint8_t>>(value->value.take());
                completer.complete_ok(std::move(ret));
              } else {
                completer.complete_ok(nullptr);
              }
              break;
            case fuchsia::ledger::Status::VALUE_TOO_LARGE:
              // TODO(thatguy): Handle a too-large value.
              FXL_LOG(FATAL) << "TODO: fallback to PageSnapshot_Get().";
            default:
              FXL_LOG(ERROR) << "PageSnapshotPromise::GetInline() failed with "
                << fidl::ToUnderlying(status);
              completer.complete_error();
          }
        });
    return bridge.consumer.promise();
  }
};

}  // namespace modular
