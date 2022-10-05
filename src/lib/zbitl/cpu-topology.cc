// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/items/cpu-topology.h>

#include <algorithm>

namespace zbitl {

fit::result<std::string_view, CpuTopologyTable> CpuTopologyTable::FromPayload(
    uint32_t item_type, zbitl::ByteView payload) {
  switch (item_type) {
    case ZBI_TYPE_CPU_TOPOLOGY:
      if (payload.size_bytes() == 0) {
        return fit::error("ZBI_TYPE_CPU_TOPOLOGY payload is empty");
      }
      if (payload.size_bytes() % sizeof(zbi_topology_node_t) == 0) {
        CpuTopologyTable result;
        result.table_ = cpp20::span{
            reinterpret_cast<const zbi_topology_node_t*>(payload.data()),
            payload.size_bytes() / sizeof(zbi_topology_node_t),
        };
        return fit::ok(result);
      }
      return fit::error("ZBI_TYPE_CPU_TOPOLOGY payload not a multiple of entry size");

    case ZBI_TYPE_CPU_CONFIG:
      if (payload.size_bytes() >= sizeof(zbi_cpu_config_t)) {
        auto conf = reinterpret_cast<const zbi_cpu_config_t*>(payload.data());
        const size_t conf_size =
            sizeof(zbi_cpu_config_t) + (conf->cluster_count * sizeof(zbi_cpu_cluster_t));
        if (payload.size_bytes() < conf_size) {
          return fit::error("ZBI_TYPE_CPU_CONFIG too small for cluster count");
        }
        CpuTopologyTable result;
        result.table_ = conf;
        return fit::ok(result);
      }
      return fit::error("ZBI_TYPE_CPU_CONFIG too small for header");

    default:
      return fit::error("invalid ZBI item type for CpuTopologyTable");
  }
}

// These functions are static in the private inner class rather than just being
// in an anonymous namespace local to this file just so that the public classes
// can have a friend declaration.

struct CpuTopologyTable::Dispatch {
  // Set up with the modern table format, just use the input as is.

  static size_t TableSize(cpp20::span<const zbi_topology_node_t> nodes) { return nodes.size(); }

  static iterator TableBegin(cpp20::span<const zbi_topology_node_t> nodes) {
    iterator result;
    result.it_ = nodes.begin();
    return result;
  }

  static iterator TableEnd(cpp20::span<const zbi_topology_node_t> nodes) {
    iterator result;
    result.it_ = nodes.end();
    return result;
  }

  // Set up with the old table format, convert on the fly.

  static size_t TableSize(const zbi_cpu_config_t* config) {
    size_t nodes = 0;
    cpp20::span clusters(config->clusters, config->cluster_count);
    for (const zbi_cpu_cluster_t& cluster : clusters) {
      // There's a node for the cluster, then a node for each CPU.
      nodes += 1 + cluster.cpu_count;
    }
    return nodes;
  }

  static iterator TableBegin(const zbi_cpu_config_t* config) {
    ConvertingIterator it;
    if (config->cluster_count > 0) {
      it.clusters_ = cpp20::span(config->clusters, config->cluster_count);
      it.logical_id_ = 0;
    }
    iterator result;
    result.it_ = it;
    return result;
  }

  static iterator TableEnd(const zbi_cpu_config_t* config) {
    iterator result;
    result.it_ = ConvertingIterator();
    return result;
  }

  static void Advance(ConvertingIterator& it) {
    ZX_ASSERT_MSG(it.logical_id_, "cannot increment default-constructed or end iterator");
    ++it.next_node_idx_;

    if (!it.cpu_idx_) {
      // This is at the node for a cluster.  Advance to its first CPU.
      it.cpu_idx_ = 0;
      return;
    }

    const uint32_t cpu_count = it.clusters_[it.cluster_idx_].cpu_count;
    if (const uint32_t cpu_idx = *it.cpu_idx_; cpu_idx < cpu_count) {
      ++*it.logical_id_;
      ++*it.cpu_idx_;
      // If there are still CPUs to process, advance to the next one; else,
      // fall through to advance to the next cluster.
      if (cpu_idx < cpu_count - 1) {
        return;
      }
    }

    // Advance to the next cluster, unless we have reached the end.
    it.cluster_node_idx_ = it.next_node_idx_;
    it.cpu_idx_ = std::nullopt;
    if (++it.cluster_idx_ == it.clusters_.size()) {
      it.logical_id_ = std::nullopt;
    }
  }

  static zbi_topology_node_t GetNode(const ConvertingIterator& it) {
    ZX_ASSERT_MSG(it.logical_id_, "cannot dereference default-constructed or end iterator");

    // First there's a node for the cluster itself.
    if (!it.cpu_idx_) {
      return zbi_topology_node_t{
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          // We don't have this data so it is a guess that little cores are
          // first.
          .entity = {.cluster = {.performance_class = it.cluster_idx_}},
      };
    }

    // Then there's a node for each CPU.
    return zbi_topology_node_t{
        .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
        .parent_index = static_cast<uint16_t>(it.cluster_node_idx_),
        .entity =
            {
                .processor =
                    {
                        .logical_ids = {*it.logical_id_},
                        .logical_id_count = 1,
                        .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                        .architecture_info =
                            {
                                .arm =
                                    {
                                        .cluster_1_id = it.cluster_idx_,
                                        .cpu_id = *it.cpu_idx_,
                                        .gic_id = *it.logical_id_,
                                    },
                            },
                    },
            },
    };
  }
};

size_t CpuTopologyTable::size() const {
  return std::visit([](const auto& table) { return Dispatch::TableSize(table); }, table_);
}

CpuTopologyTable::iterator CpuTopologyTable::begin() const {
  return std::visit([](const auto& table) { return Dispatch::TableBegin(table); }, table_);
}

CpuTopologyTable::iterator CpuTopologyTable::end() const {
  return std::visit([](const auto& table) { return Dispatch::TableEnd(table); }, table_);
}

CpuTopologyTable::ConvertingIterator& CpuTopologyTable::ConvertingIterator::operator++() {
  Dispatch::Advance(*this);
  return *this;
}

zbi_topology_node_t CpuTopologyTable::ConvertingIterator::operator*() const {
  return Dispatch::GetNode(*this);
}

}  // namespace zbitl
