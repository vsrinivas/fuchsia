// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_DEBUG_DATA_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_DEBUG_DATA_H_

#include <fuchsia/debugdata/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>

#include <map>
#include <memory>

#include <src/lib/fxl/memory/weak_ptr.h>

class DebugDataImpl {
 public:
  DebugDataImpl() : weak_factory_(this) {}

  void Bind(fidl::InterfaceRequest<fuchsia::debugdata::DebugData> request, std::string test_url,
            async_dispatcher_t* dispatcher) {
    auto inner = std::make_unique<Inner>(std::move(request), std::move(test_url), dispatcher);
    auto ptr = inner.get();
    inners_[ptr] = std::move(inner);
  }

  void BindChannel(zx::channel request, std::string test_url, async_dispatcher_t* dispatcher) {
    Bind(fidl::InterfaceRequest<fuchsia::debugdata::DebugData>(std::move(request)),
         std::move(test_url), dispatcher);
  }

 private:
  fxl::WeakPtrFactory<DebugDataImpl> weak_factory_;

  class Inner : public fuchsia::debugdata::DebugData {
   public:
    void Publish(::std::string data_sink, ::zx::vmo data) override;
    void LoadConfig(::std::string config_name, LoadConfigCallback callback) override;
    Inner(fidl::InterfaceRequest<fuchsia::debugdata::DebugData> request, std::string test_url,
          async_dispatcher_t* dispatcher);

   private:
    void CloseConnection(zx_status_t epitaph_value);

    fidl::Binding<fuchsia::debugdata::DebugData> binding_;
    fxl::WeakPtr<DebugDataImpl> parent_;
    std::string test_url_;
  };

  std::unique_ptr<Inner> Remove(Inner* ptr);

  std::map<Inner*, std::unique_ptr<Inner>> inners_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_DEBUG_DATA_H_
