// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_METRICS_DIGEST_H_
#define SRC_DEVELOPER_MEMORY_METRICS_DIGEST_H_

#include <zircon/types.h>

#include <regex>
#include <set>
#include <string>
#include <vector>

#include "src/developer/memory/metrics/capture.h"

namespace memory {

struct BucketMatch {
  const std::string name;
  const std::string process;
  const std::string vmo;
};

class Bucket {
 public:
  Bucket(const std::string& name, uint64_t size) : name_(name), size_(size) {}
  const std::string& name() const { return name_; }
  uint64_t size() const { return size_; }
  const std::vector<zx_koid_t> vmos() const { return vmos_; }

 private:
  explicit Bucket(const BucketMatch& match);
  std::string name_;
  std::regex process_;
  std::regex vmo_;
  std::vector<zx_koid_t> vmos_;
  uint64_t size_;

  friend class Digest;
};

class Digest {
 public:
  static const std::vector<const BucketMatch> kDefaultBucketMatches;
  explicit Digest(const Capture& capture,
                  const std::vector<const BucketMatch>& bucket_matches = kDefaultBucketMatches);
  zx_time_t time() const { return time_; }
  const std::vector<Bucket>& buckets() const { return buckets_; }
  const std::set<zx_koid_t>& undigested_vmos() const { return undigested_vmos_; }

 private:
  zx_time_t time_;
  std::vector<Bucket> buckets_;
  std::set<zx_koid_t> undigested_vmos_;
};

}  // namespace memory

#endif  // SRC_DEVELOPER_MEMORY_METRICS_DIGEST_H_
