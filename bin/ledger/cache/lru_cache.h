// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CACHE_LRU_CACHE_H_
#define PERIDOT_BIN_LEDGER_CACHE_LRU_CACHE_H_

#include <functional>
#include <list>
#include <map>
#include <set>
#include <vector>

#include <lib/fit/function.h>

#include "lib/fxl/logging.h"

namespace cache {
// Implements a self-populating LRU cache.
//
// This class allows the user to provide a generator for its data, and will
// cache a given number of entries, discarding least used entries once its reach
// its maximum capacity.
//
// K is the type of the key of the cached data.
// V is the type of the cached data.
// S is the type of the success status for the data generator.
template <typename K, typename V, typename S>
class LRUCache {
 public:
  // Constructor.
  //
  // |size| is the maximum capacity of the cache.
  // |ok_status| is the success status of the generator.
  // |generator| generates the value to be cached for the given key. It takes a
  //   callback to returns its result. It must return |ok_status| as a status
  //   when the request is successful. Any other return value is considered a
  //   failure.
  LRUCache(size_t size, S ok_status,
           fit::function<void(K, fit::function<void(S, V)>)> generator)
      : size_(size), ok_status_(ok_status), generator_(std::move(generator)) {}

  // Retrieves the value for |key| and returns it to |callback|.
  //
  // If the value is cached, |callback| will be called synchronously. Otherwise,
  // |generator| will be called, and depending on its implementation, |callback|
  // might be called synchronously or not.
  void Get(const K& key, fit::function<void(S, const V&)> callback) {
    auto iterator = map_.find(key);
    if (iterator != map_.end()) {
      // Move the list iterator to the front.
      auto list_iterator = iterator->second;
      values_.splice(values_.begin(), values_, list_iterator);

      // Call back.
      callback(ok_status_, iterator->second->second);
      return;
    }
    auto request_iterator = requests_.find(key);
    if (request_iterator != requests_.end()) {
      request_iterator->second.push_back(std::move(callback));
      return;
    }
    requests_[key].push_back(std::move(callback));
    generator_(key, [this, key](S status, V value) {
      auto request_iterator = requests_.find(key);
      FXL_DCHECK(request_iterator != requests_.end());
      auto callbacks = std::move(request_iterator->second);
      requests_.erase(request_iterator);

      if (status != ok_status_) {
        for (const auto& callback : callbacks) {
          callback(status, value);
        }
        return;
      }

      values_.emplace_front(key, value);
      map_[std::move(key)] = values_.begin();
      if (values_.size() > size_) {
        map_.erase(values_.rbegin()->first);
        values_.pop_back();
      }
      for (const auto& callback : callbacks) {
        callback(status, value);
      }
    });
  }

 private:
  using ValueList = std::list<std::pair<K, V>>;
  ValueList values_;
  std::map<K, typename ValueList::iterator> map_;
  std::map<K, std::vector<fit::function<void(S, const V&)>>> requests_;
  size_t size_;
  S ok_status_;
  fit::function<void(K, fit::function<void(S, V)>)> generator_;
};

}  // namespace cache

#endif  // PERIDOT_BIN_LEDGER_CACHE_LRU_CACHE_H_
