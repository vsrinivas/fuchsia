// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory-pressure.h"

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/command_line.h"

namespace sysmem = fuchsia_sysmem;

void PrintHelp() {
  Log(""
      "Usage: sysmem-memory-pressure [--contiguous] [--help] [--heap=heap] [--usage=[cpu|vulkan]] "
      "size_bytes\n");
  Log("Options:\n");
  Log(" --help           Show this message.\n");
  Log(" --contiguous     Request physically-contiguous memory\n");
  Log(" --heap           Specifies the numeric value of the sysmem heap to request memory from. By "
      "default system ram is used.\n");
  Log(" --usage          Specifies what usage should be requested from sysmem. Vulkan is the "
      "default\n");
  Log(" size_bytes       The size of the memory in bytes.\n");
}

int MemoryPressureCommand(fxl::CommandLine command_line, bool sleep) {
  if (command_line.HasOption("help")) {
    PrintHelp();
    return 0;
  }

  if (command_line.positional_args().size() != 1) {
    LogError("Missing size to allocate\n");
    PrintHelp();
    return 1;
  }

  std::string size_string = command_line.positional_args()[0];
  char* endptr;
  uint64_t size = strtoull(size_string.c_str(), &endptr, 0);
  if (endptr != size_string.c_str() + size_string.size()) {
    LogError("Invalid size %s\n", size_string.c_str());
    PrintHelp();
    return 1;
  }

  sysmem::wire::HeapType heap = sysmem::wire::HeapType::kSystemRam;
  std::string heap_string;
  if (command_line.GetOptionValue("heap", &heap_string)) {
    char* endptr;
    heap = static_cast<sysmem::wire::HeapType>(strtoull(heap_string.c_str(), &endptr, 0));
    if (endptr != heap_string.c_str() + heap_string.size()) {
      LogError("Invalid heap string: %s\n", heap_string.c_str());
      return 1;
    }
  }

  bool physically_contiguous = command_line.HasOption("contiguous");

  sysmem::wire::BufferCollectionConstraints constraints;
  std::string usage;
  if (command_line.GetOptionValue("usage", &usage)) {
    if (usage == "vulkan") {
      constraints.usage.vulkan = sysmem::wire::kVulkanUsageTransferDst;
    } else if (usage == "cpu") {
      constraints.usage.cpu = sysmem::wire::kCpuUsageRead;
    } else {
      LogError("Invalid usage %s\n", usage.c_str());
      PrintHelp();
      return 1;
    }
  } else {
    constraints.usage.vulkan = sysmem::wire::kVulkanUsageTransferDst;
  }
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  auto& mem_constraints = constraints.buffer_memory_constraints;
  mem_constraints.physically_contiguous_required = physically_contiguous;
  mem_constraints.min_size_bytes = static_cast<uint32_t>(size);
  mem_constraints.cpu_domain_supported = true;
  mem_constraints.ram_domain_supported = true;
  mem_constraints.inaccessible_domain_supported = true;
  mem_constraints.heap_permitted_count = 1;
  mem_constraints.heap_permitted[0] = heap;
  zx::channel local_endpoint, server_endpoint;
  zx::channel::create(0u, &local_endpoint, &server_endpoint);
  fdio_service_connect("/svc/fuchsia.sysmem.Allocator", server_endpoint.release());
  fidl::WireSyncClient<sysmem::Allocator> sysmem_allocator(std::move(local_endpoint));
  sysmem_allocator.SetDebugClientInfo(fidl::StringView::FromExternal(fsl::GetCurrentProcessName()),
                                      fsl::GetCurrentProcessKoid());

  zx::channel client_collection_channel, server_collection;
  zx::channel::create(0u, &client_collection_channel, &server_collection);

  sysmem_allocator.AllocateNonSharedCollection(std::move(server_collection));
  fidl::WireSyncClient<sysmem::BufferCollection> collection(std::move(client_collection_channel));

  collection.SetName(1000000, "sysmem-memory-pressure");

  collection.SetConstraints(true, std::move(constraints));

  auto res = collection.WaitForBuffersAllocated();
  if (!res.ok()) {
    LogError("Lost connection to sysmem services, error %d\n", res.status());
    return 1;
  }
  if (res->status != ZX_OK) {
    LogError("Allocation error %d\n", res->status);
    return 1;
  }
  Log("Allocated %ld bytes. Sleeping forever\n", size);

  if (sleep) {
    zx::nanosleep(zx::time::infinite());
  }

  return 0;
}
