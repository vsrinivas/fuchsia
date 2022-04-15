// Copyright 2022 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <map>

namespace forensics::feedback {
namespace {

void InsertUnique(const Annotations& annotations, const std::set<std::string>& allowlist,
                  Annotations* out) {
  for (const auto& [k, v] : annotations) {
    if (allowlist.count(k) != 0) {
      FX_CHECK(out->count(k) == 0) << "Attempting to re-insert " << k;
      out->insert({k, v});
    }
  }
}

void InsertUnique(const Annotations& annotations, Annotations* out) {
  for (const auto& [k, v] : annotations) {
    FX_CHECK(out->count(k) == 0) << "Attempting to re-insert " << k;
    out->insert({k, v});
  }
}

// Inserts all keys in |keys| with a value of |error| into |out|, if they don't already have a
// value.
void InsertMissing(const std::set<std::string>& keys, const Error error,
                   const std::set<std::string>& allowlist, Annotations* out) {
  for (const auto& key : keys) {
    if (allowlist.count(key) == 0 || out->count(key) != 0) {
      continue;
    }

    out->insert({key, error});
  }
}

template <typename Container, typename T>
void Remove(Container& c, T v) {
  c.erase(std::remove(c.begin(), c.end(), v), c.end());
}

}  // namespace

AnnotationManager::AnnotationManager(
    async_dispatcher_t* dispatcher, std::set<std::string> allowlist,
    const Annotations static_annotations, NonPlatformAnnotationProvider* non_platform_provider,
    std::vector<DynamicSyncAnnotationProvider*> dynamic_sync_providers,
    std::vector<StaticAsyncAnnotationProvider*> static_async_providers)
    : dispatcher_(dispatcher),
      allowlist_(std::move(allowlist)),
      static_annotations_(),
      non_platform_provider_(non_platform_provider),
      dynamic_sync_providers_(std::move(dynamic_sync_providers)),
      static_async_providers_(std::move(static_async_providers)) {
  InsertUnique(static_annotations, allowlist_, &static_annotations_);

  // Create a weak pointer because |this| isn't guaranteed to outlive providers.
  auto self = ptr_factory_.GetWeakPtr();
  for (auto* provider : static_async_providers_) {
    provider->GetOnce([self, provider](const Annotations annotations) {
      if (!self) {
        return;
      }

      InsertUnique(annotations, self->allowlist_, &(self->static_annotations_));

      // Remove the reference to |provider| once it has returned its annotations.
      Remove(self->static_async_providers_, provider);
      if (!self->static_async_providers_.empty()) {
        return;
      }

      // No static async providers remain so complete all calls to GetAll waiting on static
      // annotations.
      for (auto& waiting : self->waiting_for_static_) {
        waiting();
        waiting = nullptr;
      }

      Remove(self->waiting_for_static_, nullptr);
    });
  }
}

void AnnotationManager::InsertStatic(const Annotations& annotations) {
  InsertUnique(annotations, allowlist_, &static_annotations_);
}

::fpromise::promise<Annotations> AnnotationManager::GetAll(const zx::duration timeout) {
  // All static async annotations have been collected.
  if (static_async_providers_.empty()) {
    return ::fpromise::make_ok_promise(ImmediatelyAvailable());
  }

  ::fpromise::bridge<> bridge;
  auto completer = std::make_shared<::fpromise::completer<>>(std::move(bridge.completer));

  // Create a callable object that can be used to complete the call to GetAll.
  auto complete = [completer] {
    if ((*completer)) {
      completer->complete_ok();
    }
  };

  async::PostDelayedTask(dispatcher_, complete, timeout);
  waiting_for_static_.push_back(std::move(complete));

  // Create a weak pointer because |this| isn't guaranteed to outlive the promise.
  auto self = ptr_factory_.GetWeakPtr();
  return bridge.consumer.promise_or(::fpromise::error())
      .and_then([self] {
        if (!self) {
          return ::fpromise::ok(Annotations{});
        }

        Annotations annotations = self->ImmediatelyAvailable();
        for (const auto& p : self->static_async_providers_) {
          InsertMissing(p->GetKeys(), Error::kTimeout, self->allowlist_, &annotations);
        }

        return ::fpromise::ok(std::move(annotations));
      })
      .or_else([] {
        FX_LOGS(FATAL) << "Promise for waiting on annotations was incorrectly dropped";
        return ::fpromise::ok(Annotations{});
      });
}

Annotations AnnotationManager::ImmediatelyAvailable() const {
  Annotations annotations(static_annotations_);
  for (auto* provider : dynamic_sync_providers_) {
    InsertUnique(provider->Get(), allowlist_, &annotations);
  }

  if (non_platform_provider_ != nullptr) {
    InsertUnique(non_platform_provider_->Get(), &annotations);
  }

  return annotations;
}

bool AnnotationManager::IsMissingNonPlatformAnnotations() const {
  return (non_platform_provider_ == nullptr) ? false
                                             : non_platform_provider_->IsMissingAnnotations();
}

}  // namespace forensics::feedback
