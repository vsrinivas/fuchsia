// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux_platform_bus_mapper.h"

#include <errno.h>
#include <fcntl.h>

#include "linux_platform_device.h"

namespace magma {

std::unique_ptr<PlatformBusMapper::BusMapping> LinuxPlatformBusMapper::MapPageRangeBus(
    magma::PlatformBuffer* buffer, uint32_t start_page_index, uint32_t page_count) {
  if (page_count == 0 || (start_page_index + page_count) * magma::page_size() > buffer->size())
    return DRETP(nullptr, "Invalid: start_page_index %u page_count %u", start_page_index,
                 page_count);

  int udmabuf_fd = open("/dev/udmabuf", O_RDWR);
  if (udmabuf_fd < 0)
    return DRETP(nullptr, "Couldn't open /dev/udmabuf: %s", strerror(errno));

  LinuxPlatformHandle udmabuf_device(udmabuf_fd);

  auto linux_platform_buffer = static_cast<LinuxPlatformBuffer*>(buffer);

  int dma_buf_fd;
  if (!LinuxPlatformDevice::UdmabufCreate(udmabuf_device.get(), linux_platform_buffer->memfd(),
                                          start_page_index, page_count, &dma_buf_fd))
    return DRETP(nullptr, "UdmabufCreate failed");

  std::vector<uint64_t> page_addr(page_count);
  uint64_t token;
  if (!LinuxPlatformDevice::MagmaMapPageRangeBus(bus_transaction_initiator_->get(), dma_buf_fd,
                                                 start_page_index, page_count, &token,
                                                 page_addr.data()))
    return DRETP(nullptr, "MagmaMapPageRangeBus failed");

  return std::make_unique<LinuxPlatformBusMapper::BusMapping>(
      start_page_index, std::move(page_addr), dma_buf_fd, token);
}

std::unique_ptr<PlatformBusMapper> PlatformBusMapper::Create(
    std::shared_ptr<PlatformHandle> bus_transaction_initiator) {
  return std::make_unique<LinuxPlatformBusMapper>(
      std::static_pointer_cast<LinuxPlatformHandle>(bus_transaction_initiator));
}

}  // namespace magma
