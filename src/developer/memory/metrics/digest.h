// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_METRICS_DIGEST_H_
#define SRC_DEVELOPER_MEMORY_METRICS_DIGEST_H_

#include <zircon/types.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/developer/memory/metrics/bucket_match.h"
#include "src/developer/memory/metrics/capture.h"

namespace memory {

class Bucket {
 public:
  Bucket(std::string name, uint64_t size) : name_(name), size_(size) {}
  const std::string& name() const { return name_; }
  uint64_t size() const { return size_; }

 private:
  std::string name_;
  uint64_t size_;

  friend class Digester;
};

class Digester;
class Digest {
 public:
  Digest() = default;
  Digest(const Capture& capture, Digester* digester);
  zx_time_t time() const { return time_; }
  const std::vector<Bucket>& buckets() const { return buckets_; }
  const std::unordered_set<zx_koid_t>& undigested_vmos() const { return undigested_vmos_; }

 private:
  zx_time_t time_;
  std::vector<Bucket> buckets_;
  std::unordered_set<zx_koid_t> undigested_vmos_;

  friend class Digester;
};

class Digester {
 public:
  explicit Digester(const std::vector<BucketMatch>& bucket_matches);
  void Digest(const Capture& capture, Digest* digest);

 private:
  std::vector<BucketMatch> bucket_matches_;

  friend class Digest;
};

}  // namespace memory

#endif  // SRC_DEVELOPER_MEMORY_METRICS_DIGEST_H_
