// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_CONSUMER_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_CONSUMER_H_

#include <src/lib/fxl/memory/weak_ptr.h>
#include <stdlib.h>

#include <memory>

namespace netemul {
namespace data {

// Consumers are a data sink and that may be fed with data.
//
// Consumers don't care where the data came from, and will always just
// receive it.
class Consumer {
 public:
  using Ptr = fxl::WeakPtr<Consumer>;
  virtual ~Consumer() = default;
  virtual void Consume(const void* data, size_t len) = 0;
};

// Bus consumers are a data sink that may be fed with data that is
// characterized by a sender.
//
// BusConsumer acts as a proxy between a Consumer and a collection of Consumers.
//
// BusConsumer will use the sender information to avoid feeding back the
// sender's request into itself.
class BusConsumer {
 public:
  using Ptr = fxl::WeakPtr<BusConsumer>;
  virtual ~BusConsumer() = default;
  virtual void Consume(const void* data, size_t len,
                       const Consumer::Ptr& sender) = 0;
};

}  // namespace data
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_CONSUMER_H_
