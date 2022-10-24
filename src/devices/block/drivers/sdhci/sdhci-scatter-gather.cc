// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include <fbl/algorithm.h>

#include "sdhci.h"

namespace sdhci {

zx_status_t Sdhci::SdmmcRegisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo vmo,
                                    uint64_t offset, uint64_t size, uint32_t vmo_rights) {
  if (client_id >= std::size(registered_vmo_stores_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (vmo_rights == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  vmo_store::StoredVmo<OwnedVmoInfo> stored_vmo(std::move(vmo), OwnedVmoInfo{
                                                                    .offset = offset,
                                                                    .size = size,
                                                                    .rights = vmo_rights,
                                                                });
  const uint32_t read_perm = (vmo_rights & SDMMC_VMO_RIGHT_READ) ? ZX_BTI_PERM_READ : 0;
  const uint32_t write_perm = (vmo_rights & SDMMC_VMO_RIGHT_WRITE) ? ZX_BTI_PERM_WRITE : 0;
  zx_status_t status = stored_vmo.Pin(bti_, read_perm | write_perm, true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to pin VMO %u for client %u: %s", vmo_id, client_id,
           zx_status_get_string(status));
    return status;
  }

  return registered_vmo_stores_[client_id].RegisterWithKey(vmo_id, std::move(stored_vmo));
}

zx_status_t Sdhci::SdmmcUnregisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo* out_vmo) {
  if (client_id >= std::size(registered_vmo_stores_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  vmo_store::StoredVmo<OwnedVmoInfo>* const vmo_info =
      registered_vmo_stores_[client_id].GetVmo(vmo_id);
  if (!vmo_info) {
    return ZX_ERR_NOT_FOUND;
  }

  const zx_status_t status = vmo_info->vmo()->duplicate(ZX_RIGHT_SAME_RIGHTS, out_vmo);
  if (status != ZX_OK) {
    return status;
  }

  return registered_vmo_stores_[client_id].Unregister(vmo_id).status_value();
}

zx_status_t Sdhci::SdmmcRequestNew(const sdmmc_req_new_t* req, uint32_t out_response[4]) {
  if (req->client_id >= std::size(registered_vmo_stores_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (!SupportsAdma2()) {
    // TODO(fxbug.dev/106851): Add support for PIO requests.
    return ZX_ERR_NOT_SUPPORTED;
  }

  DmaDescriptorBuilder builder(*req, registered_vmo_stores_[req->client_id],
                               dma_boundary_alignment_, bti_.borrow());

  {
    fbl::AutoLock lock(&mtx_);

    // one command at a time
    if (cmd_req_ || data_req_ || pending_request_.is_pending()) {
      return ZX_ERR_SHOULD_WAIT;
    }

    if (zx_status_t status = SgStartRequest(*req, builder); status != ZX_OK) {
      return status;
    }
  }

  sync_completion_wait(&req_completion_, ZX_TIME_INFINITE);
  sync_completion_reset(&req_completion_);

  fbl::AutoLock lock(&mtx_);
  return SgFinishRequest(*req, out_response);
}

zx_status_t Sdhci::SgStartRequest(const sdmmc_req_new_t& request, DmaDescriptorBuilder& builder) {
  using BlockSizeType = decltype(BlockSize::Get().FromValue(0).reg_value());
  using BlockCountType = decltype(BlockCount::Get().FromValue(0).reg_value());

  // Every command requires that the Command Inhibit is unset.
  auto inhibit_mask = PresentState::Get().FromValue(0).set_command_inhibit_cmd(1);

  // Busy type commands must also wait for the DATA Inhibit to be 0 UNLESS
  // it's an abort command which can be issued with the data lines active.
  if ((request.cmd_flags & SDMMC_RESP_LEN_48B) && (request.cmd_flags & SDMMC_CMD_TYPE_ABORT)) {
    inhibit_mask.set_command_inhibit_dat(1);
  }

  // Wait for the inhibit masks from above to become 0 before issuing the command.
  zx_status_t status = WaitForInhibit(inhibit_mask);
  if (status != ZX_OK) {
    return status;
  }

  TransferMode transfer_mode = TransferMode::Get().FromValue(0);

  const bool is_tuning_request =
      request.cmd_idx == MMC_SEND_TUNING_BLOCK || request.cmd_idx == SD_SEND_TUNING_BLOCK;

  const auto blocksize = static_cast<BlockSizeType>(request.blocksize);

  if (is_tuning_request) {
    // The SDHCI controller has special logic to handle tuning transfers, so there is no need to set
    // up any DMA buffers.
    BlockSize::Get().FromValue(blocksize).WriteTo(&regs_mmio_buffer_);
    BlockCount::Get().FromValue(0).WriteTo(&regs_mmio_buffer_);
  } else if (request.cmd_flags & SDMMC_RESP_DATA_PRESENT) {
    if (request.blocksize > std::numeric_limits<BlockSizeType>::max()) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    if (request.blocksize == 0) {
      return ZX_ERR_INVALID_ARGS;
    }

    if (const zx_status_t status = SetUpDma(request, builder); status != ZX_OK) {
      return status;
    }

    if (builder.block_count() > std::numeric_limits<BlockCountType>::max()) {
      zxlogf(ERROR, "Block count (%lu) exceeds the maximum (%u)", builder.block_count(),
             std::numeric_limits<BlockCountType>::max());
      return ZX_ERR_OUT_OF_RANGE;
    }

    transfer_mode.set_dma_enable(1).set_multi_block(builder.block_count() > 1 ? 1 : 0);

    const auto blockcount = static_cast<BlockCountType>(builder.block_count());

    BlockSize::Get().FromValue(blocksize).WriteTo(&regs_mmio_buffer_);
    BlockCount::Get().FromValue(blockcount).WriteTo(&regs_mmio_buffer_);
  } else {
    BlockSize::Get().FromValue(0).WriteTo(&regs_mmio_buffer_);
    BlockCount::Get().FromValue(0).WriteTo(&regs_mmio_buffer_);
  }

  Command command = Command::Get().FromValue(0);
  PrepareCmd(request, &transfer_mode, &command);

  Argument::Get().FromValue(request.arg).WriteTo(&regs_mmio_buffer_);

  // Clear any pending interrupts before starting the transaction.
  auto irq_mask = InterruptSignalEnable::Get().ReadFrom(&regs_mmio_buffer_);
  InterruptStatus::Get().FromValue(irq_mask.reg_value()).WriteTo(&regs_mmio_buffer_);

  pending_request_.Init(request);

  // Unmask and enable interrupts
  EnableInterrupts();

  // Start command
  transfer_mode.WriteTo(&regs_mmio_buffer_);
  command.WriteTo(&regs_mmio_buffer_);

  return ZX_OK;
}

zx_status_t Sdhci::SetUpDma(const sdmmc_req_new_t& request, DmaDescriptorBuilder& builder) {
  const cpp20::span buffers{request.buffers_list, request.buffers_count};
  zx_status_t status;
  for (const auto& buffer : buffers) {
    if ((status = builder.ProcessBuffer(buffer)) != ZX_OK) {
      return status;
    }
  }

  size_t descriptor_size;
  if (Capabilities0::Get().ReadFrom(&regs_mmio_buffer_).v3_64_bit_system_address_support()) {
    const cpp20::span descriptors{reinterpret_cast<AdmaDescriptor96*>(iobuf_.virt()),
                                  kDmaDescCount};
    descriptor_size = sizeof(descriptors[0]);
    status = builder.BuildDmaDescriptors(descriptors);
  } else {
    const cpp20::span descriptors{reinterpret_cast<AdmaDescriptor64*>(iobuf_.virt()),
                                  kDmaDescCount};
    descriptor_size = sizeof(descriptors[0]);
    status = builder.BuildDmaDescriptors(descriptors);
  }

  if (status != ZX_OK) {
    return status;
  }

  status = iobuf_.CacheOp(ZX_VMO_OP_CACHE_CLEAN, 0, builder.descriptor_count() * descriptor_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to clean cache: %s", zx_status_get_string(status));
    return status;
  }

  AdmaSystemAddress::Get(0).FromValue(Lo32(iobuf_.phys())).WriteTo(&regs_mmio_buffer_);
  AdmaSystemAddress::Get(1).FromValue(Hi32(iobuf_.phys())).WriteTo(&regs_mmio_buffer_);
  return ZX_OK;
}

zx_status_t Sdhci::SgFinishRequest(const sdmmc_req_new_t& request, uint32_t out_response[4]) {
  if (pending_request_.cmd_done) {
    memcpy(out_response, pending_request_.response, sizeof(uint32_t) * 4);
  }

  if (request.cmd_flags & SDMMC_CMD_TYPE_ABORT) {
    // SDHCI spec section 3.8.2: reset the data line after an abort to discard data in the buffer.
    [[maybe_unused]] auto _ =
        WaitForReset(SoftwareReset::Get().FromValue(0).set_reset_cmd(1).set_reset_dat(1));
  }

  const InterruptStatus interrupt_status = pending_request_.status;
  pending_request_.Reset();

  if (!interrupt_status.error()) {
    return ZX_OK;
  }

  if (interrupt_status.tuning_error()) {
    zxlogf(ERROR, "Tuning error");
  }
  if (interrupt_status.adma_error()) {
    zxlogf(ERROR, "ADMA error cmd%u", request.cmd_idx);
  }
  if (interrupt_status.auto_cmd_error()) {
    zxlogf(ERROR, "Auto cmd error cmd%u", request.cmd_idx);
  }
  if (interrupt_status.current_limit_error()) {
    zxlogf(ERROR, "Current limit error cmd%u", request.cmd_idx);
  }
  if (interrupt_status.data_end_bit_error()) {
    zxlogf(ERROR, "Data end bit error cmd%u", request.cmd_idx);
  }
  if (interrupt_status.data_crc_error()) {
    if (request.suppress_error_messages) {
      zxlogf(DEBUG, "Data CRC error cmd%u", request.cmd_idx);
    } else {
      zxlogf(ERROR, "Data CRC error cmd%u", request.cmd_idx);
    }
  }
  if (interrupt_status.data_timeout_error()) {
    zxlogf(ERROR, "Data timeout error cmd%u", request.cmd_idx);
  }
  if (interrupt_status.command_index_error()) {
    zxlogf(ERROR, "Command index error cmd%u", request.cmd_idx);
  }
  if (interrupt_status.command_end_bit_error()) {
    zxlogf(ERROR, "Command end bit error cmd%u", request.cmd_idx);
  }
  if (interrupt_status.command_crc_error()) {
    if (request.suppress_error_messages) {
      zxlogf(DEBUG, "Command CRC error cmd%u", request.cmd_idx);
    } else {
      zxlogf(ERROR, "Command CRC error cmd%u", request.cmd_idx);
    }
  }
  if (interrupt_status.command_timeout_error()) {
    if (request.suppress_error_messages) {
      zxlogf(DEBUG, "Command timeout error cmd%u", request.cmd_idx);
    } else {
      zxlogf(ERROR, "Command timeout error cmd%u", request.cmd_idx);
    }
  }
  if (interrupt_status.reg_value() ==
      InterruptStatusEnable::Get().FromValue(0).set_error(1).reg_value()) {
    // Log an unknown error only if no other bits were set.
    zxlogf(ERROR, "Unknown error cmd%u", request.cmd_idx);
  }

  return ZX_ERR_IO;
}

template <typename DescriptorType>
zx_status_t Sdhci::DmaDescriptorBuilder::BuildDmaDescriptors(
    cpp20::span<DescriptorType> out_descriptors) {
  if (total_size_ % request_.blocksize != 0) {
    zxlogf(ERROR, "Total buffer size (%lu) is not a multiple of the request block size (%u)",
           total_size_, request_.blocksize);
    return ZX_ERR_INVALID_ARGS;
  }

  const cpp20::span<const fzl::PinnedVmo::Region> regions{regions_.data(), region_count_};
  auto desc_it = out_descriptors.begin();
  for (const fzl::PinnedVmo::Region region : regions) {
    if (desc_it == out_descriptors.end()) {
      zxlogf(ERROR, "Not enough DMA descriptors to handle request");
      return ZX_ERR_OUT_OF_RANGE;
    }

    if constexpr (sizeof(desc_it->address) == sizeof(uint32_t)) {
      if (Hi32(region.phys_addr) != 0) {
        zxlogf(ERROR, "64-bit physical address supplied for 32-bit DMA");
        return ZX_ERR_NOT_SUPPORTED;
      }
      desc_it->address = static_cast<uint32_t>(region.phys_addr);
    } else {
      desc_it->address = region.phys_addr;
    }

    // Should be enforced by ProcessBuffer.
    ZX_DEBUG_ASSERT(region.size > 0);
    ZX_DEBUG_ASSERT(region.size <= kMaxDescriptorLength);

    desc_it->length = static_cast<uint16_t>(region.size == kMaxDescriptorLength ? 0 : region.size);
    desc_it->attr = Adma2DescriptorAttributes::Get()
                        .set_valid(1)
                        .set_type(Adma2DescriptorAttributes::kTypeData)
                        .reg_value();
    desc_it++;
  }

  if (desc_it == out_descriptors.begin()) {
    zxlogf(ERROR, "No buffers were provided for the transfer");
    return ZX_ERR_INVALID_ARGS;
  }

  // The above check verifies that we have at least on descriptor. Set the end bit on the last
  // descriptor as per the SDHCI ADMA2 spec.
  desc_it[-1].attr = Adma2DescriptorAttributes::Get(desc_it[-1].attr).set_end(1).reg_value();

  descriptor_count_ = desc_it - out_descriptors.begin();
  return ZX_OK;
}

zx_status_t Sdhci::DmaDescriptorBuilder::ProcessBuffer(const sdmmc_buffer_region_t& buffer) {
  total_size_ += buffer.size;

  fzl::PinnedVmo::Region region_buffer[SDMMC_PAGES_COUNT];
  zx::result<size_t> region_count;
  if (buffer.type == SDMMC_BUFFER_TYPE_VMO_HANDLE) {
    region_count = GetPinnedRegions(zx::unowned_vmo(buffer.buffer.vmo), buffer,
                                    {region_buffer, std::size(region_buffer)});
  } else if (buffer.type == SDMMC_BUFFER_TYPE_VMO_ID) {
    region_count =
        GetPinnedRegions(buffer.buffer.vmo_id, buffer, {region_buffer, std::size(region_buffer)});
  } else {
    return ZX_ERR_INVALID_ARGS;
  }

  if (region_count.is_error()) {
    return region_count.error_value();
  }

  return AppendRegions({region_buffer, region_count.value()});
}

zx::result<size_t> Sdhci::DmaDescriptorBuilder::GetPinnedRegions(
    uint32_t vmo_id, const sdmmc_buffer_region_t& buffer,
    cpp20::span<fzl::PinnedVmo::Region> out_regions) {
  vmo_store::StoredVmo<OwnedVmoInfo>* const stored_vmo =
      registered_vmos_.GetVmo(buffer.buffer.vmo_id);
  if (stored_vmo == nullptr) {
    zxlogf(ERROR, "No VMO %u for client %u", buffer.buffer.vmo_id, request_.client_id);
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  // Make sure that this request would not cause the controller to violate the rights of the VMO,
  // as we may not have an IOMMU to otherwise prevent it.
  if (!(request_.cmd_flags & SDMMC_CMD_READ) &&
      !(stored_vmo->meta().rights & SDMMC_VMO_RIGHT_READ)) {
    // Write request, controller reads from this VMO and writes to the card.
    zxlogf(ERROR, "Request would cause controller to read from write-only VMO");
    return zx::error(ZX_ERR_ACCESS_DENIED);
  }
  if ((request_.cmd_flags & SDMMC_CMD_READ) &&
      !(stored_vmo->meta().rights & SDMMC_VMO_RIGHT_WRITE)) {
    // Read request, controller reads from the card and writes to this VMO.
    zxlogf(ERROR, "Request would cause controller to write to read-only VMO");
    return zx::error(ZX_ERR_ACCESS_DENIED);
  }

  size_t region_count = 0;
  const zx_status_t status =
      stored_vmo->GetPinnedRegions(buffer.offset + stored_vmo->meta().offset, buffer.size,
                                   out_regions.data(), out_regions.size(), &region_count);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get pinned regions: %s", zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::ok(region_count);
}

zx::result<size_t> Sdhci::DmaDescriptorBuilder::GetPinnedRegions(
    zx::unowned_vmo vmo, const sdmmc_buffer_region_t& buffer,
    cpp20::span<fzl::PinnedVmo::Region> out_regions) {
  const uint64_t kPageSize = zx_system_get_page_size();
  const uint64_t kPageMask = kPageSize - 1;

  if (pmt_count_ >= pmts_.size()) {
    zxlogf(ERROR, "Too many unowned VMOs specified, maximum is %zu", pmts_.size());
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  const uint64_t page_offset = buffer.offset & kPageMask;
  const uint64_t page_count = fbl::round_up(buffer.size + page_offset, kPageSize) / kPageSize;

  const uint32_t options =
      (request_.cmd_flags & SDMMC_CMD_READ) ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;

  zx_paddr_t phys[SDMMC_PAGES_COUNT];

  if (page_count == 0) {
    zxlogf(ERROR, "Buffer has no pages");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (page_count > std::size(phys)) {
    zxlogf(ERROR, "Buffer has too many pages, maximum is %zu", std::size(phys));
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  zx_status_t status = bti_->pin(options, *vmo, buffer.offset - page_offset, page_count * kPageSize,
                                 phys, page_count, &pmts_[pmt_count_]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to pin unowned VMO: %s", zx_status_get_string(status));
    return zx::error(status);
  }

  pmt_count_++;

  ZX_DEBUG_ASSERT(!out_regions.empty());  // This assumption simplifies the following logic.

  out_regions[0].phys_addr = phys[0] + page_offset;
  out_regions[0].size = kPageSize - page_offset;

  // Check for any pages that happen to be both contiguous and increasing in physical addresses.
  // Such pages, if there are any, can be combined into a single DMA descriptor to enable larger
  // transfers.

  size_t last_region = 0;
  for (size_t paddr_count = 1; paddr_count < page_count; paddr_count++) {
    if ((out_regions[last_region].phys_addr + out_regions[last_region].size) == phys[paddr_count]) {
      // The current region is contiguous with this physical address, increase it by the page size.
      out_regions[last_region].size += kPageSize;
    } else if (++last_region < out_regions.size()) {
      // The current region is not contiguous with this physical address, create a new region.
      out_regions[last_region].phys_addr = phys[paddr_count];
      out_regions[last_region].size = kPageSize;
    } else {
      // Ran out of regions.
      zxlogf(ERROR, "Buffer has too many regions, maximum is %zu", out_regions.size());
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
  }

  // Adjust the last region size based on the offset into the first page and the total size of the
  // buffer.
  out_regions[last_region].size -= (page_count * kPageSize) - buffer.size - page_offset;

  return zx::ok(last_region + 1);
}

zx_status_t Sdhci::DmaDescriptorBuilder::AppendRegions(
    const cpp20::span<const fzl::PinnedVmo::Region> regions) {
  if (regions.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }

  fzl::PinnedVmo::Region current_region{0, 0};
  auto vmo_regions_it = regions.begin();
  for (; region_count_ < regions_.size(); region_count_++) {
    // Current region is invalid, fetch a new one from the input list.
    if (current_region.size == 0) {
      // No more regions left to process.
      if (vmo_regions_it == regions.end()) {
        return ZX_OK;
      }
      if (vmo_regions_it->size == 0) {
        return ZX_ERR_INVALID_ARGS;
      }

      current_region = *vmo_regions_it++;
    }

    // Default to an invalid region so that the next iteration fetches another one from the input
    // list. If this region is divided due to a boundary or size restriction, the next region will
    // remain valid so that processing of the original region will continue.
    fzl::PinnedVmo::Region next_region{0, 0};

    if (dma_boundary_alignment_) {
      const zx_paddr_t aligned_start =
          fbl::round_down(current_region.phys_addr, dma_boundary_alignment_);
      const zx_paddr_t aligned_end = fbl::round_down(
          current_region.phys_addr + current_region.size - 1, dma_boundary_alignment_);

      if (aligned_start != aligned_end) {
        // Crossing a boundary, split the DMA buffer in two.
        const size_t first_size =
            aligned_start + dma_boundary_alignment_ - current_region.phys_addr;
        next_region.size = current_region.size - first_size;
        next_region.phys_addr = current_region.phys_addr + first_size;
        current_region.size = first_size;
      }
    }

    // The region size is greater than the maximum, split it into two or more smaller regions.
    if (current_region.size > kMaxDescriptorLength) {
      const size_t size_diff = current_region.size - kMaxDescriptorLength;
      if (next_region.size) {
        next_region.phys_addr -= size_diff;
      } else {
        next_region.phys_addr = current_region.phys_addr + kMaxDescriptorLength;
      }
      next_region.size += size_diff;
      current_region.size = kMaxDescriptorLength;
    }

    regions_[region_count_] = current_region;
    current_region = next_region;
  }

  // If processing did not reach the end of the VMO regions or the current region is still valid we
  // must have hit the end of the output region buffer.
  return vmo_regions_it == regions.end() && current_region.size == 0 ? ZX_OK : ZX_ERR_OUT_OF_RANGE;
}

void Sdhci::SgHandleInterrupt(const InterruptStatus status) {
  if (status.ErrorInterrupt()) {
    pending_request_.status = status;
    pending_request_.status.set_error(1);
    SgErrorRecovery();
    return;
  }

  // Clear the interrupt status to indicate that a normal interrupt was handled.
  pending_request_.status = InterruptStatus::Get().FromValue(0);
  if (status.buffer_read_ready() && SgDataStageReadReady()) {
    return;
  }
  if (status.command_complete() && SgCmdStageComplete()) {
    return;
  }
  if (status.transfer_complete()) {
    SgTransferComplete();
  }
}

bool Sdhci::SgCmdStageComplete() {
  const uint32_t response_0 = Response::Get(0).ReadFrom(&regs_mmio_buffer_).reg_value();
  const uint32_t response_1 = Response::Get(1).ReadFrom(&regs_mmio_buffer_).reg_value();
  const uint32_t response_2 = Response::Get(2).ReadFrom(&regs_mmio_buffer_).reg_value();
  const uint32_t response_3 = Response::Get(3).ReadFrom(&regs_mmio_buffer_).reg_value();

  // Read the response data.
  if (pending_request_.cmd_flags & SDMMC_RESP_LEN_136) {
    if (quirks_ & SDHCI_QUIRK_STRIP_RESPONSE_CRC) {
      pending_request_.response[0] = (response_3 << 8) | ((response_2 >> 24) & 0xFF);
      pending_request_.response[1] = (response_2 << 8) | ((response_1 >> 24) & 0xFF);
      pending_request_.response[2] = (response_1 << 8) | ((response_0 >> 24) & 0xFF);
      pending_request_.response[3] = (response_0 << 8);
    } else if (quirks_ & SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER) {
      pending_request_.response[0] = (response_0 << 8);
      pending_request_.response[1] = (response_1 << 8) | ((response_0 >> 24) & 0xFF);
      pending_request_.response[2] = (response_2 << 8) | ((response_1 >> 24) & 0xFF);
      pending_request_.response[3] = (response_3 << 8) | ((response_2 >> 24) & 0xFF);
    } else {
      pending_request_.response[0] = response_0;
      pending_request_.response[1] = response_1;
      pending_request_.response[2] = response_2;
      pending_request_.response[3] = response_3;
    }
  } else if (pending_request_.cmd_flags & (SDMMC_RESP_LEN_48 | SDMMC_RESP_LEN_48B)) {
    pending_request_.response[0] = response_0;
  }

  pending_request_.cmd_done = true;

  // We're done if the command has no data stage or if the data stage completed early
  if (pending_request_.data_done) {
    SgCompleteRequest();
  }

  return pending_request_.data_done;
}

bool Sdhci::SgTransferComplete() {
  pending_request_.data_done = true;
  if (pending_request_.cmd_done) {
    SgCompleteRequest();
  }

  return pending_request_.cmd_done;
}

bool Sdhci::SgDataStageReadReady() {
  if ((pending_request_.cmd_idx == MMC_SEND_TUNING_BLOCK) ||
      (pending_request_.cmd_idx == SD_SEND_TUNING_BLOCK)) {
    // This is the final interrupt expected for tuning transfers, so mark both command and data
    // phases complete.
    pending_request_.cmd_done = true;
    pending_request_.data_done = true;
    SgCompleteRequest();
    return true;
  }

  return false;
}

void Sdhci::SgErrorRecovery() {
  // Reset internal state machines
  {
    SoftwareReset::Get().ReadFrom(&regs_mmio_buffer_).set_reset_cmd(1).WriteTo(&regs_mmio_buffer_);
    [[maybe_unused]] auto _ = WaitForReset(SoftwareReset::Get().FromValue(0).set_reset_cmd(1));
  }
  {
    SoftwareReset::Get().ReadFrom(&regs_mmio_buffer_).set_reset_dat(1).WriteTo(&regs_mmio_buffer_);
    [[maybe_unused]] auto _ = WaitForReset(SoftwareReset::Get().FromValue(0).set_reset_dat(1));
  }

  // Complete any pending txn with error status
  SgCompleteRequest();
}

void Sdhci::SgCompleteRequest() {
  DisableInterrupts();
  sync_completion_signal(&req_completion_);
}

}  // namespace sdhci
