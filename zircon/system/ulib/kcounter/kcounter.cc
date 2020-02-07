// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "kcounter.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <lib/fdio/io.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

#include <utility>
#include <vector>

#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <fbl/unique_fd.h>

namespace {

constexpr char kVmoFileDir[] = "/boot/kernel";

std::vector<fbl::String> SplitString(fbl::String input, char delimiter) {
  std::vector<fbl::String> result;

  const char* start = input.begin();
  for (auto end = start; end != input.end(); start = end + 1) {
    end = start;
    while (end != input.end() && *end != delimiter) {
      ++end;
    }
    result.push_back(fbl::String(start, end - start));
  }
  return result;
}

}  // anonymous namespace

namespace kcounter {

VmoToInspectMapper::VmoToInspectMapper() : inspector_() {
  fbl::unique_fd dir_fd(open(kVmoFileDir, O_RDONLY | O_DIRECTORY));
  if (!dir_fd) {
    initialization_status_ = ZX_ERR_IO;
    return;
  }

  {
    fbl::unique_fd desc_fd(openat(dir_fd.get(), counters::DescriptorVmo::kVmoName, O_RDONLY));
    if (!desc_fd) {
      fprintf(stderr, "%s/%s: %s\n", kVmoFileDir, counters::DescriptorVmo::kVmoName,
              strerror(errno));
      initialization_status_ = ZX_ERR_IO;
      return;
    }
    zx::vmo vmo;
    zx_status_t status = fdio_get_vmo_exact(desc_fd.get(), vmo.reset_and_get_address());
    if (status != ZX_OK) {
      fprintf(stderr, "fdio_get_vmo_exact: %s: %s\n", counters::DescriptorVmo::kVmoName,
              zx_status_get_string(status));
      initialization_status_ = ZX_ERR_IO;
      return;
    }
    uint64_t size;
    status = vmo.get_size(&size);
    if (status != ZX_OK) {
      fprintf(stderr, "cannot get %s VMO size: %s\n", counters::DescriptorVmo::kVmoName,
              zx_status_get_string(status));
      initialization_status_ = ZX_ERR_IO;
      return;
    }
    status = desc_mapper_.Map(std::move(vmo), size, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      fprintf(stderr, "cannot map %s VMO: %s\n", counters::DescriptorVmo::kVmoName,
              zx_status_get_string(status));
      initialization_status_ = ZX_ERR_IO;
      return;
    }
    status = desc_mapper_.Map(std::move(vmo), size, ZX_VM_PERM_READ);
    desc_ = reinterpret_cast<counters::DescriptorVmo*>(desc_mapper_.start());
    if (desc_->magic != counters::DescriptorVmo::kMagic) {
      fprintf(stderr, "%s: magic number %" PRIu64 " != expected %" PRIu64 "\n",
              counters::DescriptorVmo::kVmoName, desc_->magic, counters::DescriptorVmo::kMagic);
      initialization_status_ = ZX_ERR_IO;
      return;
    }
    status = desc_mapper_.Map(std::move(vmo), size, ZX_VM_PERM_READ);
    if (size < sizeof(*desc_) + desc_->descriptor_table_size) {
      fprintf(stderr, "%s size %#" PRIx64 " too small for %" PRIu64 " bytes of descriptor table\n",
              counters::DescriptorVmo::kVmoName, size, desc_->descriptor_table_size);
      initialization_status_ = ZX_ERR_IO;
      return;
    }
  }

  fbl::unique_fd arena_fd(openat(dir_fd.get(), counters::kArenaVmoName, O_RDONLY));
  if (!arena_fd) {
    fprintf(stderr, "%s/%s: %s\n", kVmoFileDir, counters::kArenaVmoName, strerror(errno));
    initialization_status_ = ZX_ERR_IO;
    return;
  }
  zx::vmo vmo;
  zx_status_t status = fdio_get_vmo_exact(arena_fd.get(), vmo.reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "fdio_get_vmo_exact: %s: %s\n", counters::kArenaVmoName,
            zx_status_get_string(status));
    initialization_status_ = ZX_ERR_IO;
    return;
  }
  uint64_t size;
  status = vmo.get_size(&size);
  if (status != ZX_OK) {
    fprintf(stderr, "cannot get %s VMO size: %s\n", counters::kArenaVmoName,
            zx_status_get_string(status));
    initialization_status_ = ZX_ERR_IO;
    return;
  }
  if (size < desc_->max_cpus * desc_->num_counters() * sizeof(int64_t)) {
    fprintf(stderr, "%s size %#" PRIx64 " too small for %" PRIu64 " CPUS * %" PRIu64 " counters\n",
            counters::kArenaVmoName, size, desc_->max_cpus, desc_->num_counters());
    initialization_status_ = ZX_ERR_IO;
    return;
  }
  status = arena_mapper_.Map(std::move(vmo), size, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    fprintf(stderr, "cannot map %s VMO: %s\n", counters::kArenaVmoName,
            zx_status_get_string(status));
    initialization_status_ = ZX_ERR_IO;
    return;
  }

  arena_ = reinterpret_cast<int64_t*>(arena_mapper_.start());

  BuildCounterToMetricVMOMapping();

  initialization_status_ = ZX_OK;
}

bool VmoToInspectMapper::ShouldInclude(const counters::Descriptor& entry) {
  if (entry.type != counters::Type::kSum) {
    // Only 'sum' counters are supported for export to inspect currently.
    return false;
  }

  // This list is hand-selected for utility of reporting but can be freely
  // updated as desired.

  // These counters are always included. The strings are the full name of the
  // counter.
  static constexpr const char* kByName[] = {
      "channel.messages",         //
      "profile.create",           //
      "profile.set",              //
      "init.target.time.msec",    //
      "init.userboot.time.msec",  //
      "handles.duped",            //
      "handles.live",             //
      "handles.made",             //
  };

  for (size_t i = 0; i < countof(kByName); ++i) {
    if (strcmp(kByName[i], entry.name) == 0) {
      return true;
    }
  }

  // Any counters starting with these prefixes are included.
  // TODO(scottmg): It would be nice to filter these to only-if-non-zero.
  static constexpr const char* kByPrefix[] = {
      "exceptions.",      //
      "policy.deny.",     //
      "policy.kill.",     //
      "port.full.count",  //
      "boot.timeline.",   //
      "thread.suspend",   //
  };
  for (size_t i = 0; i < countof(kByPrefix); ++i) {
    if (strncmp(kByPrefix[i], entry.name, strlen(kByPrefix[i])) == 0) {
      return true;
    }
  }

  return false;
}

void VmoToInspectMapper::BuildCounterToMetricVMOMapping() {
  auto& root = inspector_.GetRoot();
  metric_by_index_.resize(desc_->num_counters());

  for (size_t i = 0; i < desc_->num_counters(); ++i) {
    const auto& entry = desc_->descriptor_table[i];
    if (!ShouldInclude(entry)) {
      continue;
    }

    auto parts = SplitString(entry.name, '.');
    ZX_ASSERT(parts.size() > 1);
    inspect::Node* parent_object = &root;
    fbl::String current_name;
    for (size_t j = 0; j < parts.size() - 1; ++j) {
      current_name = fbl::String::Concat({current_name, parts[j]});
      if (intermediate_nodes_.find(current_name) == intermediate_nodes_.end()) {
        intermediate_nodes_[current_name] =
            parent_object->CreateChild(std::string(parts[j].data()));
      }
      parent_object = &intermediate_nodes_[current_name];
      current_name = fbl::String::Concat({current_name, "."});
    }

    metric_by_index_[i] = parent_object->CreateInt(entry.name, 0u);
  }
}

zx_status_t VmoToInspectMapper::UpdateInspectVMO() {
  if (initialization_status_ != ZX_OK) {
    return initialization_status_;
  }

  // Don't hit kernel-exposed VMO more than 1/s, regardless of how often a
  // request is made.
  zx::time current_time = zx::clock::get_monotonic();
  if (last_update_ + zx::sec(1) < current_time) {
    last_update_ = current_time;

    // The current data that's passed back only:
    // - reports from our "interesting" allowlist
    // - reports only the summarized values (not per-cpu values)
    //
    // Regardless of the number of times this function is called, the data will
    // not be updated more frequently than once per second.

    for (size_t i = 0; i < desc_->num_counters(); ++i) {
      const auto& entry = desc_->descriptor_table[i];
      if (!ShouldInclude(entry)) {
        continue;
      }

      int64_t value = 0;
      for (uint64_t cpu = 0; cpu < desc_->max_cpus; ++cpu) {
        const int64_t cpu_value = arena_[(cpu * desc_->num_counters()) + i];
        value += cpu_value;
      }

      metric_by_index_[i].Set(value);
    }
  }

  return ZX_OK;
}

zx_status_t VmoToInspectMapper::GetInspectVMO(zx::vmo* vmo) {
  if (initialization_status_ != ZX_OK) {
    return initialization_status_;
  }

  zx_status_t update_status = UpdateInspectVMO();
  if (update_status != ZX_OK) {
    return update_status;
  }

  *vmo = inspector_.DuplicateVmo();

  if (vmo->get() != ZX_HANDLE_INVALID) {
    return ZX_OK;
  } else {
    return ZX_ERR_INTERNAL;
  }
}

}  // namespace kcounter
