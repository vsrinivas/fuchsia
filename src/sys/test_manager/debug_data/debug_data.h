// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_DEBUG_DATA_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_DEBUG_DATA_H_

#include <fuchsia/debugdata/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <src/lib/fxl/memory/weak_ptr.h>

#include "common.h"

/// Pair of test url corresponding to moniker and data sinks.
using DebugInfo = std::pair<std::string, std::vector<DataSinkDump>>;

/// Notify when the connection is closed.
using NotifyOnClose = fit::function<void(std::string moniker)>;

/// This class is not thread safe.
class DebugDataImpl {
 public:
  DebugDataImpl();
  ~DebugDataImpl();

  void Bind(fidl::InterfaceRequest<fuchsia::debugdata::DebugData> request, std::string moniker,
            std::string test_url, async_dispatcher_t* dispatcher, NotifyOnClose notify = nullptr) {
    auto inner =
        std::make_unique<Inner>(std::move(request), weak_factory_.GetWeakPtr(), std::move(moniker),
                                std::move(test_url), std::move(notify), dispatcher);
    auto ptr = inner.get();
    inners_.emplace(ptr, std::move(inner));
  }

  void BindChannel(zx::channel request, std::string moniker, std::string test_url,
                   async_dispatcher_t* dispatcher, NotifyOnClose notify = nullptr) {
    Bind(fidl::InterfaceRequest<fuchsia::debugdata::DebugData>(std::move(request)),
         std::move(moniker), std::move(test_url), dispatcher, std::move(notify));
  }

  std::optional<DebugInfo> TakeData(const std::string& moniker);
  void AddData(const std::string& moniker, const std::string& test_url, std::string data_sink,
               zx::vmo vmo);

 private:
  fxl::WeakPtrFactory<DebugDataImpl> weak_factory_;

  class Inner : public fuchsia::debugdata::DebugData {
   public:
    void Publish(::std::string data_sink, zx::vmo data,
                 fidl::InterfaceRequest<fuchsia::debugdata::DebugDataVmoToken> vmo_token) override;
    void LoadConfig(::std::string config_name, LoadConfigCallback callback) override;
    Inner(fidl::InterfaceRequest<fuchsia::debugdata::DebugData> request,
          fxl::WeakPtr<DebugDataImpl> parent, std::string moniker, std::string test_url,
          NotifyOnClose notify, async_dispatcher_t* dispatcher);

    ~Inner() override;

   private:
    void CloseConnection(zx_status_t epitaph_value);
    void DestroyAndNotify();

    std::string test_url_;
    std::string moniker_;
    NotifyOnClose notify_;
    fxl::WeakPtr<DebugDataImpl> parent_;
    fidl::Binding<fuchsia::debugdata::DebugData> binding_;
  };

  std::unique_ptr<Inner> Remove(Inner* ptr);

  std::map<Inner*, std::unique_ptr<Inner>> inners_;
  std::map<std::string, DebugInfo> data_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_DEBUG_DATA_H_
