// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_LEGACY_DATA_PIPE_DATA_PIPE_DRAINER_H_
#define LIB_MTL_LEGACY_DATA_PIPE_DATA_PIPE_DRAINER_H_

#include <mojo/environment/async_waiter.h>

#include "lib/ftl/macros.h"
#include "mojo/public/cpp/environment/environment.h"
#include "mojo/public/cpp/system/data_pipe.h"

namespace mtl {

class LegacyDataPipeDrainer {
 public:
  class Client {
   public:
    virtual void OnDataAvailable(const void* data, size_t num_bytes) = 0;
    virtual void OnDataComplete() = 0;

   protected:
    virtual ~Client() {}
  };

  LegacyDataPipeDrainer(Client* client,
                        const MojoAsyncWaiter* waiter =
                            mojo::Environment::GetDefaultAsyncWaiter());
  ~LegacyDataPipeDrainer();

  void Start(mojo::ScopedDataPipeConsumerHandle source);

 private:
  void ReadData();
  void WaitForData();
  static void WaitComplete(void* context, MojoResult result);

  Client* client_;
  mojo::ScopedDataPipeConsumerHandle source_;
  const MojoAsyncWaiter* waiter_;
  MojoAsyncWaitID wait_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LegacyDataPipeDrainer);
};

}  // namespace mtl

#endif  // LIB_MTL_LEGACY_DATA_PIPE_DATA_PIPE_DRAINER_H_
