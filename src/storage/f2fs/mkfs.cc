// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/mkfs.h"

#include <getopt.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>

#include <safemath/checked_math.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/f2fs/segment.h"

namespace f2fs {

void MkfsWorker::PrintCurrentOption() const {
  std::cerr << "f2fs mkfs label = " << mkfs_options_.label << std::endl;
  std::cerr << "f2fs mkfs heap-based allocation = " << mkfs_options_.heap_based_allocation
            << std::endl;
  std::cerr << "f2fs mkfs overprovision ratio = " << mkfs_options_.overprovision_ratio << std::endl;
  std::cerr << "f2fs mkfs segments per sector = " << mkfs_options_.segs_per_sec << std::endl;
  std::cerr << "f2fs mkfs sectors per zone = " << mkfs_options_.secs_per_zone << std::endl;
  std::cerr << "f2fs mkfs extension list = " << mkfs_options_.extension_list << std::endl;
}

zx::result<std::unique_ptr<Bcache>> MkfsWorker::DoMkfs() {
  InitGlobalParameters();

  if (zx_status_t ret = GetDeviceInfo(); ret != ZX_OK)
    return zx::error(ret);

  if (zx_status_t ret = FormatDevice(); ret != ZX_OK)
    return zx::error(ret);
  return zx::ok(std::move(bc_));
}

void AsciiToUnicode(const std::string_view in_string, std::u16string &out_string) {
  out_string.assign(in_string.begin(), in_string.end());
}

void MkfsWorker::InitGlobalParameters() {
  static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

  params_.sector_size = kDefaultSectorSize;
  params_.sectors_per_blk = kDefaultSectorsPerBlock;
  params_.blks_per_seg = kDefaultBlocksPerSegment;
  params_.reserved_segments = 0;
  params_.overprovision = mkfs_options_.overprovision_ratio;
  params_.segs_per_sec = mkfs_options_.segs_per_sec;
  params_.secs_per_zone = mkfs_options_.secs_per_zone;
  params_.heap = (mkfs_options_.heap_based_allocation ? 1 : 0);
  if (mkfs_options_.label.length() != 0) {
    ZX_ASSERT(mkfs_options_.label.length() + 1 <= kVolumeLabelLength);
    memcpy(params_.vol_label, mkfs_options_.label.c_str(), mkfs_options_.label.length() + 1);
  } else {
    memset(params_.vol_label, 0, sizeof(params_.vol_label));

    params_.vol_label[0] = 'F';
    params_.vol_label[1] = '2';
    params_.vol_label[2] = 'F';
    params_.vol_label[3] = 'S';
    params_.vol_label[4] = '\0';
  }
  params_.device_name = nullptr;

  params_.extension_list = mkfs_options_.extension_list;
}

zx_status_t MkfsWorker::GetDeviceInfo() {
#ifdef __Fuchsia__
  fuchsia_hardware_block::wire::BlockInfo info;

  bc_->GetDevice()->BlockGetInfo(&info);

  params_.sector_size = info.block_size;
  params_.sectors_per_blk = kBlockSize / info.block_size;
  params_.total_sectors = info.block_count;
  params_.start_sector = kSuperblockStart;

  if (info.block_size < kDefaultSectorSize || info.block_size > kBlockSize) {
    std::cerr << "Error: Block size " << info.block_size << " is not supported" << std::endl;
    return ZX_ERR_INVALID_ARGS;
  }

  if (info.flags & fuchsia_hardware_block::wire::Flag::kReadonly) {
    std::cerr << "Error: Failed to format f2fs: read only block device" << std::endl;
    return ZX_ERR_INVALID_ARGS;
  }

#else   // __Fuchsia__
  params_.sector_size = kDefaultSectorSize;
  params_.sectors_per_blk = kBlockSize / kDefaultSectorSize;
  params_.total_sectors = bc_->Maxblk() * kDefaultSectorSize / kBlockSize;
  params_.start_sector = kSuperblockStart;
#endif  // __Fuchsia__

  return ZX_OK;
}

void MkfsWorker::ConfigureExtensionList() {
  super_block_.extension_count = 0;
  memset(super_block_.extension_list, 0, sizeof(super_block_.extension_list));

  int name_len;
  int i = 0;

  for (const char *ext : kMediaExtList) {
    name_len = static_cast<int>(strlen(ext));
    memcpy(super_block_.extension_list[i++], ext, name_len);
  }
  super_block_.extension_count = i;

  if (!params_.extension_list.length())
    return;

  // add user ext list
  char *ue = strtok(const_cast<char *>(params_.extension_list.c_str()), ",");
  while (ue != nullptr) {
    name_len = static_cast<int>(strlen(ue));
    memcpy(super_block_.extension_list[i++], ue, name_len);
    ue = strtok(nullptr, ",");
    if (i >= kMaxExtension)
      break;
  }

  super_block_.extension_count = i;
}

zx_status_t MkfsWorker::WriteToDisk(FsBlock &buf, block_t bno) {
#ifdef __Fuchsia__
  return bc_->Writeblk(bno, buf.GetData().data());
#else   // __Fuchsia__
  return bc_->Writeblk(bno, buf.GetData());
#endif  // __Fuchsia__
}

zx::result<uint32_t> MkfsWorker::GetCalculatedOp(uint32_t user_op) const {
  uint32_t max_op = 0;
  uint32_t max_user_segments = 0;

  if (user_op < 100 && user_op > 0)
    return zx::ok(user_op);

  for (uint32_t calc_op = 1; calc_op < 100; ++calc_op) {
    uint32_t reserved_segments =
        (2 * (100 / calc_op + 1) + kNrCursegType) * super_block_.segs_per_sec;

    if ((safemath::CheckSub(LeToCpu(super_block_.segment_count_main), 2).ValueOrDie()) <
        reserved_segments) {
      continue;
    }
    uint32_t user_segments =
        (super_block_.segment_count_main -
         (safemath::CheckSub(super_block_.segment_count_main, reserved_segments) * calc_op / 100) -
         reserved_segments)
            .ValueOrDie();

    if (user_segments > max_user_segments &&
        safemath::CheckSub(super_block_.segment_count_main, 2).ValueOrDie() >= reserved_segments) {
      max_user_segments = user_segments;
      max_op = calc_op;
    }
  }

  if (max_op == 0)
    return zx::error(ZX_ERR_INVALID_ARGS);
  return zx::ok(max_op);
}

zx_status_t MkfsWorker::PrepareSuperblock() {
  super_block_.magic = CpuToLe(uint32_t{kF2fsSuperMagic});
  super_block_.major_ver = CpuToLe(kMajorVersion);
  super_block_.minor_ver = CpuToLe(kMinorVersion);

  uint32_t log_sectorsize = static_cast<uint32_t>(log2(static_cast<double>(params_.sector_size)));
  uint32_t log_sectors_per_block =
      static_cast<uint32_t>(log2(static_cast<double>(params_.sectors_per_blk)));
  uint32_t log_blocksize = log_sectorsize + log_sectors_per_block;
  uint32_t log_blks_per_seg =
      static_cast<uint32_t>(log2(static_cast<double>(params_.blks_per_seg)));

  super_block_.log_sectorsize = CpuToLe(log_sectorsize);

  if (log_sectorsize < kMinLogSectorSize || log_sectorsize > kMaxLogSectorSize) {
    FX_LOGS(ERROR) << "Error: Failed to get the sector size: " << params_.sector_size << "!";
    return ZX_ERR_INVALID_ARGS;
  }

  super_block_.log_sectors_per_block = CpuToLe(log_sectors_per_block);

  if (log_sectors_per_block < 0 ||
      log_sectors_per_block > (kMaxLogSectorSize - kMinLogSectorSize)) {
    FX_LOGS(ERROR) << "Error: Failed to get sectors per block: " << params_.sectors_per_blk << "!";
    return ZX_ERR_INVALID_ARGS;
  }

  super_block_.log_blocksize = CpuToLe(log_blocksize);
  super_block_.log_blocks_per_seg = CpuToLe(log_blks_per_seg);

  if (log_blks_per_seg !=
      static_cast<uint32_t>(log2(static_cast<double>(kDefaultBlocksPerSegment)))) {
    FX_LOGS(ERROR) << "Error: Failed to get block per segment: " << params_.blks_per_seg << "!";
    return ZX_ERR_INVALID_ARGS;
  }

  super_block_.segs_per_sec = CpuToLe(params_.segs_per_sec);
  super_block_.secs_per_zone = CpuToLe(params_.secs_per_zone);
  uint64_t blk_size_bytes = 1 << log_blocksize;
  uint32_t segment_size_bytes = static_cast<uint32_t>(blk_size_bytes * params_.blks_per_seg);
  uint32_t zone_size_bytes = static_cast<uint32_t>(blk_size_bytes * params_.secs_per_zone *
                                                   params_.segs_per_sec * params_.blks_per_seg);

  super_block_.checksum_offset = 0;

  super_block_.block_count =
      CpuToLe((params_.total_sectors * params_.sector_size) / blk_size_bytes);

  uint64_t zone_align_start_offset =
      (params_.start_sector * params_.sector_size + 2 * kBlockSize + zone_size_bytes - 1) /
          zone_size_bytes * zone_size_bytes -
      params_.start_sector * params_.sector_size;

  if (params_.start_sector % params_.sectors_per_blk) {
    FX_LOGS(WARNING) << "WARN: Align start sector number in a unit of pages";
    FX_LOGS(WARNING) << "\ti.e., start sector: " << params_.start_sector
                     << ", ofs: " << params_.start_sector % params_.sectors_per_blk
                     << " (sectors per page: " << params_.sectors_per_blk << ")";
  }

  super_block_.segment_count = static_cast<uint32_t>(CpuToLe(
      (safemath::CheckSub(params_.total_sectors * params_.sector_size, zone_align_start_offset) /
       segment_size_bytes)
          .ValueOrDie()));

  super_block_.segment0_blkaddr =
      static_cast<uint32_t>(CpuToLe(zone_align_start_offset / blk_size_bytes));
  super_block_.cp_blkaddr = super_block_.segment0_blkaddr;

  super_block_.segment_count_ckpt = CpuToLe(kNumberOfCheckpointPack);

  super_block_.sit_blkaddr =
      CpuToLe(LeToCpu(super_block_.segment0_blkaddr) +
              (LeToCpu(super_block_.segment_count_ckpt) * (1 << log_blks_per_seg)));

  uint32_t blocks_for_sit =
      (safemath::CheckSub(LeToCpu(super_block_.segment_count) + kSitEntryPerBlock, 1) /
       kSitEntryPerBlock)
          .ValueOrDie();

  uint32_t sit_segments =
      (safemath::CheckSub(blocks_for_sit + params_.blks_per_seg, 1) / params_.blks_per_seg)
          .ValueOrDie();

  super_block_.segment_count_sit = CpuToLe(sit_segments * 2);

  super_block_.nat_blkaddr =
      CpuToLe(LeToCpu(super_block_.sit_blkaddr) +
              (LeToCpu(super_block_.segment_count_sit) * params_.blks_per_seg));

  uint32_t total_valid_blks_available =
      (safemath::CheckSub(
           LeToCpu(super_block_.segment_count),
           LeToCpu(super_block_.segment_count_ckpt) + LeToCpu(super_block_.segment_count_sit)) *
       params_.blks_per_seg)
          .ValueOrDie();

  uint32_t blocks_for_nat =
      (safemath::CheckSub(total_valid_blks_available + kNatEntryPerBlock, 1) / kNatEntryPerBlock)
          .ValueOrDie();

  super_block_.segment_count_nat = CpuToLe(safemath::checked_cast<uint32_t>(
      (safemath::CheckSub(blocks_for_nat + params_.blks_per_seg, 1) / params_.blks_per_seg)
          .ValueOrDie()));

  // The number of node segments should not be exceeded a "Threshold".
  // This number resizes NAT bitmap area in a CP page.
  // So the threshold is determined not to overflow one CP page
  uint32_t sit_bitmap_size =
      ((LeToCpu(super_block_.segment_count_sit) / 2) << log_blks_per_seg) / 8;
  uint32_t max_sit_bitmap_size = std::min(sit_bitmap_size, kMaxSitBitmapSize);

  uint32_t max_nat_bitmap_size;
  if (max_sit_bitmap_size >
      kChecksumOffset - sizeof(Checkpoint) + 1 + (kDefaultBlocksPerSegment / kBitsPerByte)) {
    max_nat_bitmap_size = kChecksumOffset - sizeof(Checkpoint) + 1;
    super_block_.cp_payload = (max_sit_bitmap_size + kBlockSize - 1) / kBlockSize;
  } else {
    max_nat_bitmap_size = kChecksumOffset - sizeof(Checkpoint) + 1 - max_sit_bitmap_size;
    super_block_.cp_payload = 0;
  }

  uint32_t max_nat_segments = (max_nat_bitmap_size * 8) >> log_blks_per_seg;

  if (LeToCpu(super_block_.segment_count_nat) > max_nat_segments)
    super_block_.segment_count_nat = CpuToLe(max_nat_segments);

  super_block_.segment_count_nat = CpuToLe(LeToCpu(super_block_.segment_count_nat) * 2);

  super_block_.ssa_blkaddr =
      CpuToLe(LeToCpu(super_block_.nat_blkaddr) +
              LeToCpu(super_block_.segment_count_nat) * params_.blks_per_seg);

  total_valid_blks_available =
      (LeToCpu(super_block_.segment_count) -
       (LeToCpu(super_block_.segment_count_ckpt) + LeToCpu(super_block_.segment_count_sit) +
        LeToCpu(super_block_.segment_count_nat))) *
      params_.blks_per_seg;

  uint32_t blocks_for_ssa = total_valid_blks_available / params_.blks_per_seg + 1;

  super_block_.segment_count_ssa = CpuToLe(safemath::checked_cast<uint32_t>(
      ((blocks_for_ssa + safemath::CheckSub(params_.blks_per_seg, 1)) / params_.blks_per_seg)
          .ValueOrDie()));

  uint64_t total_meta_segments =
      LeToCpu(super_block_.segment_count_ckpt) + LeToCpu(super_block_.segment_count_sit) +
      LeToCpu(super_block_.segment_count_nat) + LeToCpu(super_block_.segment_count_ssa);

  if (uint64_t diff =
          total_meta_segments % static_cast<uint64_t>(params_.segs_per_sec * params_.secs_per_zone);
      diff != 0) {
    super_block_.segment_count_ssa = static_cast<uint32_t>(CpuToLe(
        LeToCpu(super_block_.segment_count_ssa) +
        (params_.segs_per_sec * params_.secs_per_zone - safemath::checked_cast<uint32_t>(diff))));
  }

  super_block_.main_blkaddr =
      CpuToLe(LeToCpu(super_block_.ssa_blkaddr) +
              (LeToCpu(super_block_.segment_count_ssa) * params_.blks_per_seg));

  super_block_.segment_count_main = CpuToLe(safemath::checked_cast<uint32_t>(
      safemath::CheckSub(
          LeToCpu(super_block_.segment_count),
          (LeToCpu(super_block_.segment_count_ckpt)) + LeToCpu(super_block_.segment_count_sit) +
              LeToCpu(super_block_.segment_count_nat) + LeToCpu(super_block_.segment_count_ssa))
          .ValueOrDie()));

  super_block_.section_count =
      CpuToLe(LeToCpu(super_block_.segment_count_main) / params_.segs_per_sec);

  super_block_.segment_count_main =
      CpuToLe(LeToCpu(super_block_.section_count) * params_.segs_per_sec);

  auto op = GetCalculatedOp(params_.overprovision);
  if (op.is_error()) {
    FX_LOGS(WARNING) << "Error: Device size is not sufficient for F2FS volume";
    return ZX_ERR_NO_SPACE;
  }
  params_.overprovision = op.value();

  // The number of reserved_segments depends on the OP value. Assuming OP is 20%, 20% of a dirty
  // segment is invalid. That is, running GC on 5 dirty segments can obtain one free segment.
  // Therefore, the required reserved_segments can be obtained with 100 / OP.
  // If the data page is moved to another segment due to GC, the dnode that refers to it should be
  // modified. This requires twice the reserved_segments.
  // Current active segments have the next segment in advance, of which require 6 additional
  // segments.
  params_.reserved_segments =
      (2 * (100 / params_.overprovision + 1) + kNrCursegType) * params_.segs_per_sec;

  if ((safemath::CheckSub(LeToCpu(super_block_.segment_count_main), 2).ValueOrDie()) <
      params_.reserved_segments) {
    FX_LOGS(ERROR) << "Error: Device size is not sufficient for F2FS volume, more segment needed ="
                   << params_.reserved_segments - (LeToCpu(super_block_.segment_count_main) - 2);
    return ZX_ERR_NO_SPACE;
  }

  memcpy(super_block_.uuid, uuid::Uuid::Generate().bytes(), 16);

  std::string vol_label(reinterpret_cast<char const *>(params_.vol_label));
  std::u16string volume_name;

  AsciiToUnicode(vol_label, volume_name);

  volume_name.copy(reinterpret_cast<char16_t *>(super_block_.volume_name), vol_label.size());
  super_block_.volume_name[vol_label.size()] = '\0';

  super_block_.node_ino = CpuToLe(1U);
  super_block_.meta_ino = CpuToLe(2U);
  super_block_.root_ino = CpuToLe(3U);

  uint32_t total_zones =
      ((safemath::CheckSub(LeToCpu(super_block_.segment_count_main), 1) / params_.segs_per_sec) /
       params_.secs_per_zone)
          .ValueOrDie();
  if (total_zones <= kNrCursegType) {
    FX_LOGS(ERROR) << "Error: " << total_zones << " zones: Need more zones by shrinking zone size";
    return ZX_ERR_NO_SPACE;
  }

  if (params_.heap) {
    params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)] =
        (total_zones - 1) * params_.segs_per_sec * params_.secs_per_zone +
        ((params_.secs_per_zone - 1) * params_.segs_per_sec);
    params_.cur_seg[static_cast<int>(CursegType::kCursegWarmNode)] =
        params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)] -
        params_.segs_per_sec * params_.secs_per_zone;
    params_.cur_seg[static_cast<int>(CursegType::kCursegColdNode)] =
        params_.cur_seg[static_cast<int>(CursegType::kCursegWarmNode)] -
        params_.segs_per_sec * params_.secs_per_zone;
    params_.cur_seg[static_cast<int>(CursegType::kCursegHotData)] =
        params_.cur_seg[static_cast<int>(CursegType::kCursegColdNode)] -
        params_.segs_per_sec * params_.secs_per_zone;
    params_.cur_seg[static_cast<int>(CursegType::kCursegColdData)] = 0;
    params_.cur_seg[static_cast<int>(CursegType::kCursegWarmData)] =
        params_.cur_seg[static_cast<int>(CursegType::kCursegColdData)] +
        params_.segs_per_sec * params_.secs_per_zone;
  } else {
    params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)] = 0;
    params_.cur_seg[static_cast<int>(CursegType::kCursegWarmNode)] =
        params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)] +
        params_.segs_per_sec * params_.secs_per_zone;
    params_.cur_seg[static_cast<int>(CursegType::kCursegColdNode)] =
        params_.cur_seg[static_cast<int>(CursegType::kCursegWarmNode)] +
        params_.segs_per_sec * params_.secs_per_zone;
    params_.cur_seg[static_cast<int>(CursegType::kCursegHotData)] =
        params_.cur_seg[static_cast<int>(CursegType::kCursegColdNode)] +
        params_.segs_per_sec * params_.secs_per_zone;
    params_.cur_seg[static_cast<int>(CursegType::kCursegColdData)] =
        params_.cur_seg[static_cast<int>(CursegType::kCursegHotData)] +
        params_.segs_per_sec * params_.secs_per_zone;
    params_.cur_seg[static_cast<int>(CursegType::kCursegWarmData)] =
        params_.cur_seg[static_cast<int>(CursegType::kCursegColdData)] +
        params_.segs_per_sec * params_.secs_per_zone;
  }

  ConfigureExtensionList();

  return ZX_OK;
}

zx_status_t MkfsWorker::InitSitArea() {
  FsBlock sit_block;
  uint32_t segment_count_sit_blocks = (1 << LeToCpu(super_block_.log_blocks_per_seg)) *
                                      (LeToCpu(super_block_.segment_count_sit) / 2);

  block_t sit_segment_block_num = LeToCpu(super_block_.sit_blkaddr);

  for (block_t index = 0; index < segment_count_sit_blocks; ++index) {
    if (zx_status_t ret = WriteToDisk(sit_block, sit_segment_block_num + index); ret != ZX_OK) {
      FX_LOGS(ERROR) << "Error: While zeroing out the sit area on disk!!!";
      return ret;
    }
  }

  return ZX_OK;
}

zx_status_t MkfsWorker::InitNatArea() {
  FsBlock nat_block;
  uint32_t segment_count_nat_blocks = (1 << LeToCpu(super_block_.log_blocks_per_seg)) *
                                      (LeToCpu(super_block_.segment_count_nat) / 2);

  block_t nat_segment_block_num = LeToCpu(super_block_.nat_blkaddr);

  for (block_t index = 0; index < segment_count_nat_blocks; ++index) {
    if (zx_status_t ret = WriteToDisk(nat_block, nat_segment_block_num + index); ret != ZX_OK) {
      FX_LOGS(ERROR) << "Error: While zeroing out the nat area on disk!!!";
      return ret;
    }
  }

  return ZX_OK;
}

zx_status_t MkfsWorker::WriteCheckPointPack() {
  FsBlock checkpoint_block;
#ifdef __Fuchsia__
  Checkpoint *checkpoint = reinterpret_cast<Checkpoint *>(checkpoint_block.GetData().data());
#else   // __Fuchsia__
  Checkpoint *checkpoint = reinterpret_cast<Checkpoint *>(checkpoint_block.GetData());
#endif  // __Fuchsia__

  FsBlock summary_block;
#ifdef __Fuchsia__
  SummaryBlock *summary = reinterpret_cast<SummaryBlock *>(summary_block.GetData().data());
#else   // __Fuchsia__
  SummaryBlock *summary = reinterpret_cast<SummaryBlock *>(summary_block.GetData());
#endif  // __Fuchsia__

  // 1. cp page 1 of checkpoint pack 1
  checkpoint->checkpoint_ver = 1;
  checkpoint->cur_node_segno[0] =
      CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)]);
  checkpoint->cur_node_segno[1] =
      CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegWarmNode)]);
  checkpoint->cur_node_segno[2] =
      CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegColdNode)]);
  checkpoint->cur_data_segno[0] =
      CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegHotData)]);
  checkpoint->cur_data_segno[1] =
      CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegWarmData)]);
  checkpoint->cur_data_segno[2] =
      CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegColdData)]);
  for (int i = 3; i < kMaxActiveNodeLogs; ++i) {
    checkpoint->cur_node_segno[i] = 0xffffffff;
    checkpoint->cur_data_segno[i] = 0xffffffff;
  }

  checkpoint->cur_node_blkoff[0] = CpuToLe(uint16_t{1});
  checkpoint->cur_data_blkoff[0] = CpuToLe(uint16_t{1});
  checkpoint->valid_block_count = CpuToLe(2UL);
  checkpoint->rsvd_segment_count = CpuToLe(params_.reserved_segments);
  checkpoint->overprov_segment_count = CpuToLe(safemath::checked_cast<uint32_t>(
      (safemath::CheckSub(LeToCpu(super_block_.segment_count_main),
                          LeToCpu(checkpoint->rsvd_segment_count)) *
       params_.overprovision / 100)
          .ValueOrDie()));
  checkpoint->overprov_segment_count = CpuToLe(LeToCpu(checkpoint->overprov_segment_count) +
                                               LeToCpu(checkpoint->rsvd_segment_count));

  // main segments - reserved segments - (node + data segments)
  checkpoint->free_segment_count = CpuToLe(safemath::checked_cast<uint32_t>(
      safemath::CheckSub(LeToCpu(super_block_.segment_count_main), kNrCursegType).ValueOrDie()));

  checkpoint->user_block_count = CpuToLe(safemath::checked_cast<uint64_t>(
      (safemath::CheckSub(LeToCpu(checkpoint->free_segment_count) + kNrCursegType,
                          LeToCpu(checkpoint->overprov_segment_count)) *
       params_.blks_per_seg)
          .ValueOrDie()));

  checkpoint->cp_pack_total_block_count = CpuToLe(8U + LeToCpu(super_block_.cp_payload));
  checkpoint->ckpt_flags |= CpuToLe(static_cast<uint32_t>(CpFlag::kCpUmountFlag));
  checkpoint->cp_pack_start_sum = CpuToLe(1U + LeToCpu(super_block_.cp_payload));
  checkpoint->valid_node_count = CpuToLe(1U);
  checkpoint->valid_inode_count = CpuToLe(1U);
  checkpoint->next_free_nid = CpuToLe(LeToCpu(super_block_.root_ino) + 1);

  checkpoint->sit_ver_bitmap_bytesize = CpuToLe(
      ((LeToCpu(super_block_.segment_count_sit) / 2) << LeToCpu(super_block_.log_blocks_per_seg)) /
      8);

  checkpoint->nat_ver_bitmap_bytesize = CpuToLe(
      ((LeToCpu(super_block_.segment_count_nat) / 2) << LeToCpu(super_block_.log_blocks_per_seg)) /
      8);

  checkpoint->checksum_offset = CpuToLe(kChecksumOffset);

  uint32_t crc = F2fsCalCrc32(kF2fsSuperMagic, checkpoint, LeToCpu(checkpoint->checksum_offset));
  *(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(checkpoint) +
                                 LeToCpu(checkpoint->checksum_offset))) = crc;

  block_t cp_segment_block_num = LeToCpu(super_block_.segment0_blkaddr);

  if (zx_status_t ret = WriteToDisk(checkpoint_block, cp_segment_block_num); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Error: While writing the ckp to disk!!!";
    return ret;
  }

  for (uint32_t i = 0; i < super_block_.cp_payload; ++i) {
    ++cp_segment_block_num;
    FsBlock zero_buffer;
    if (zx_status_t ret = WriteToDisk(zero_buffer, cp_segment_block_num); ret != ZX_OK) {
      FX_LOGS(ERROR) << "Error: While zeroing out the sit bitmap area on disk!!!";
      return ret;
    }
  }

  // 2. Prepare and write Segment summary for data blocks
  memset(summary, 0, sizeof(SummaryBlock));
  SetSumType((&summary->footer), kSumTypeData);

  summary->entries[0].nid = super_block_.root_ino;
  summary->entries[0].ofs_in_node = 0;

  ++cp_segment_block_num;
  if (zx_status_t ret = WriteToDisk(summary_block, cp_segment_block_num); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Error: While writing the summary_block to disk!!!";
    return ret;
  }

  // 3. Fill segment summary for data block to zero.
  memset(summary, 0, sizeof(SummaryBlock));
  SetSumType((&summary->footer), kSumTypeData);

  ++cp_segment_block_num;
  if (zx_status_t ret = WriteToDisk(summary_block, cp_segment_block_num); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Error: While writing the summary_block to disk!!!";
    return ret;
  }

  // 4. Fill segment summary for data block to zero.
  memset(summary, 0, sizeof(SummaryBlock));
  SetSumType((&summary->footer), kSumTypeData);

  // inode sit for root
  summary->n_sits = CpuToLe(uint16_t{6});
  summary->sit_j.entries[0].segno = checkpoint->cur_node_segno[0];
  summary->sit_j.entries[0].se.vblocks =
      CpuToLe(uint16_t{(static_cast<int>(CursegType::kCursegHotNode) << 10) | 1});
  SetValidBitmap(0, summary->sit_j.entries[0].se.valid_map);
  summary->sit_j.entries[1].segno = checkpoint->cur_node_segno[1];
  summary->sit_j.entries[1].se.vblocks =
      CpuToLe(uint16_t{(static_cast<int>(CursegType::kCursegWarmNode) << 10)});
  summary->sit_j.entries[2].segno = checkpoint->cur_node_segno[2];
  summary->sit_j.entries[2].se.vblocks =
      CpuToLe(uint16_t{(static_cast<int>(CursegType::kCursegColdNode) << 10)});

  // data sit for root
  summary->sit_j.entries[3].segno = checkpoint->cur_data_segno[0];
  summary->sit_j.entries[3].se.vblocks =
      CpuToLe(uint16_t{(static_cast<uint16_t>(CursegType::kCursegHotData) << 10) | 1});
  SetValidBitmap(0, summary->sit_j.entries[3].se.valid_map);
  summary->sit_j.entries[4].segno = checkpoint->cur_data_segno[1];
  summary->sit_j.entries[4].se.vblocks =
      CpuToLe(uint16_t{(static_cast<int>(CursegType::kCursegWarmData) << 10)});
  summary->sit_j.entries[5].segno = checkpoint->cur_data_segno[2];
  summary->sit_j.entries[5].se.vblocks =
      CpuToLe(uint16_t{(static_cast<int>(CursegType::kCursegColdData) << 10)});

  ++cp_segment_block_num;
  if (zx_status_t ret = WriteToDisk(summary_block, cp_segment_block_num); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Error: While writing the summary_block to disk!!!";
    return ret;
  }

  // 5. Prepare and write Segment summary for node blocks
  memset(summary, 0, sizeof(SummaryBlock));
  SetSumType((&summary->footer), kSumTypeNode);

  summary->entries[0].nid = super_block_.root_ino;
  summary->entries[0].ofs_in_node = 0;

  ++cp_segment_block_num;
  if (zx_status_t ret = WriteToDisk(summary_block, cp_segment_block_num); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Error: While writing the summary_block to disk!!!";
    return ret;
  }

  // 6. Fill segment summary for data block to zero.
  memset(summary, 0, sizeof(SummaryBlock));
  SetSumType((&summary->footer), kSumTypeNode);

  ++cp_segment_block_num;
  if (zx_status_t ret = WriteToDisk(summary_block, cp_segment_block_num); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Error: While writing the summary_block to disk!!!";
    return ret;
  }

  // 7. Fill segment summary for data block to zero.
  memset(summary, 0, sizeof(SummaryBlock));
  SetSumType((&summary->footer), kSumTypeNode);

  ++cp_segment_block_num;
  if (zx_status_t ret = WriteToDisk(summary_block, cp_segment_block_num); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Error: While writing the summary_block to disk!!!";
    return ret;
  }

  // 8. cp page2
  ++cp_segment_block_num;
  if (zx_status_t ret = WriteToDisk(checkpoint_block, cp_segment_block_num); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Error: While writing the checkpoint to disk!!!";
    return ret;
  }

  // 9. cp pages of check point pack 2
  // Initiatialize other checkpoint pack with version zero
  checkpoint->checkpoint_ver = 0;

  crc = F2fsCalCrc32(kF2fsSuperMagic, checkpoint, LeToCpu(checkpoint->checksum_offset));
  *(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(checkpoint) +
                                 LeToCpu(checkpoint->checksum_offset))) = crc;

  cp_segment_block_num = (LeToCpu(super_block_.segment0_blkaddr) + params_.blks_per_seg);
  if (zx_status_t ret = WriteToDisk(checkpoint_block, cp_segment_block_num); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Error: While writing the checkpoint to disk!!!";
    return ret;
  }

  for (uint32_t i = 0; i < super_block_.cp_payload; ++i) {
    ++cp_segment_block_num;
    FsBlock zero_buffer;
    if (zx_status_t ret = WriteToDisk(zero_buffer, cp_segment_block_num); ret != ZX_OK) {
      FX_LOGS(ERROR) << "Error: While zeroing out the sit bitmap area on disk!!!";
      return ret;
    }
  }

  cp_segment_block_num +=
      checkpoint->cp_pack_total_block_count - 1 - LeToCpu(super_block_.cp_payload);
  if (zx_status_t ret = WriteToDisk(checkpoint_block, cp_segment_block_num); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Error: While writing the checkpoint to disk!!!";
    return ret;
  }

  return ZX_OK;
}

zx_status_t MkfsWorker::WriteSuperblock() {
  FsBlock super_block;
#ifdef __Fuchsia__
  uint8_t *super_block_buff = reinterpret_cast<uint8_t *>(super_block.GetData().data());
#else   // __Fuchsia__
  uint8_t *super_block_buff = reinterpret_cast<uint8_t *>(super_block.GetData());
#endif  // __Fuchsia__

  memcpy(super_block_buff + kSuperOffset, &super_block_, sizeof(super_block_));

  for (block_t index = 0; index < 2; ++index) {
    if (zx_status_t ret = WriteToDisk(super_block, index); ret != ZX_OK) {
      FX_LOGS(ERROR) << "Error: While while writing supe_blk on disk!!! index : " << index;
      return ret;
    }
  }

  return ZX_OK;
}

zx_status_t MkfsWorker::WriteRootInode() {
  FsBlock raw_block;
#ifdef __Fuchsia__
  Node *raw_node = reinterpret_cast<Node *>(raw_block.GetData().data());
#else   // __Fuchsia__
  Node *raw_node = reinterpret_cast<Node *>(raw_block.GetData());
#endif  // __Fuchsia__

  raw_node->footer.nid = super_block_.root_ino;
  raw_node->footer.ino = super_block_.root_ino;
  raw_node->footer.cp_ver = CpuToLe(1UL);
  raw_node->footer.next_blkaddr = CpuToLe(
      LeToCpu(super_block_.main_blkaddr) +
      params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)] * params_.blks_per_seg + 1);

  raw_node->i.i_mode = CpuToLe(uint16_t{0x41ed});
  raw_node->i.i_links = CpuToLe(2U);
  raw_node->i.i_uid = CpuToLe(getuid());
  raw_node->i.i_gid = CpuToLe(getgid());

  uint64_t blk_size_bytes = 1 << LeToCpu(super_block_.log_blocksize);
  raw_node->i.i_size = CpuToLe(1 * blk_size_bytes);  // dentry
  raw_node->i.i_blocks = CpuToLe(2UL);

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  raw_node->i.i_atime = static_cast<uint64_t>(cur_time.tv_sec);
  raw_node->i.i_atime_nsec = static_cast<uint32_t>(cur_time.tv_nsec);
  raw_node->i.i_ctime = static_cast<uint64_t>(cur_time.tv_sec);
  raw_node->i.i_ctime_nsec = static_cast<uint32_t>(cur_time.tv_nsec);
  raw_node->i.i_mtime = static_cast<uint64_t>(cur_time.tv_sec);
  raw_node->i.i_mtime_nsec = static_cast<uint32_t>(cur_time.tv_nsec);
  raw_node->i.i_generation = 0;
  raw_node->i.i_xattr_nid = 0;
  raw_node->i.i_flags = 0;
  raw_node->i.i_current_depth = CpuToLe(1U);

  uint64_t data_blk_nor =
      LeToCpu(super_block_.main_blkaddr) +
      params_.cur_seg[static_cast<int>(CursegType::kCursegHotData)] * params_.blks_per_seg;
  raw_node->i.i_addr[0] = static_cast<uint32_t>(CpuToLe(data_blk_nor));

  raw_node->i.i_ext.fofs = 0;
  raw_node->i.i_ext.blk_addr = static_cast<uint32_t>(CpuToLe(data_blk_nor));
  raw_node->i.i_ext.len = CpuToLe(1U);

  block_t node_segment_block_num = LeToCpu(super_block_.main_blkaddr);
  node_segment_block_num += safemath::checked_cast<uint64_t>(
      params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)] * params_.blks_per_seg);

  return WriteToDisk(raw_block, node_segment_block_num);
}

zx_status_t MkfsWorker::UpdateNatRoot() {
  FsBlock raw_nat_block;
#ifdef __Fuchsia__
  NatBlock *nat_block = reinterpret_cast<NatBlock *>(raw_nat_block.GetData().data());
#else   // __Fuchsia__
  NatBlock *nat_block = reinterpret_cast<NatBlock *>(raw_nat_block.GetData());
#endif  // __Fuchsia__

  // update root
  nat_block->entries[super_block_.root_ino].block_addr =
      CpuToLe(LeToCpu(super_block_.main_blkaddr) +
              params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)] * params_.blks_per_seg);
  nat_block->entries[super_block_.root_ino].ino = super_block_.root_ino;

  // update node nat
  nat_block->entries[super_block_.node_ino].block_addr = CpuToLe(1U);
  nat_block->entries[super_block_.node_ino].ino = super_block_.node_ino;

  // update meta nat
  nat_block->entries[super_block_.meta_ino].block_addr = CpuToLe(1U);
  nat_block->entries[super_block_.meta_ino].ino = super_block_.meta_ino;

  block_t nat_segment_block_num = LeToCpu(super_block_.nat_blkaddr);

  return WriteToDisk(raw_nat_block, nat_segment_block_num);
}

zx_status_t MkfsWorker::AddDefaultDentryRoot() {
  FsBlock raw_dent_block;
#ifdef __Fuchsia__
  DentryBlock *dent_block = reinterpret_cast<DentryBlock *>(raw_dent_block.GetData().data());
#else   // __Fuchsia__
  DentryBlock *dent_block = reinterpret_cast<DentryBlock *>(raw_dent_block.GetData());
#endif  // __Fuchsia__

  dent_block->dentry[0].hash_code = 0;
  dent_block->dentry[0].ino = super_block_.root_ino;
  dent_block->dentry[0].name_len = CpuToLe(uint16_t{1});
  dent_block->dentry[0].file_type = static_cast<uint8_t>(FileType::kFtDir);
  memcpy(dent_block->filename[0], ".", 1);

  dent_block->dentry[1].hash_code = 0;
  dent_block->dentry[1].ino = super_block_.root_ino;
  dent_block->dentry[1].name_len = CpuToLe(uint16_t{2});
  dent_block->dentry[1].file_type = static_cast<uint8_t>(FileType::kFtDir);
  memcpy(dent_block->filename[1], "..", 2);

  // bitmap for . and ..
  dent_block->dentry_bitmap[0] = (1 << 1) | (1 << 0);
  block_t data_block_num =
      LeToCpu(super_block_.main_blkaddr) +
      params_.cur_seg[static_cast<int>(CursegType::kCursegHotData)] * params_.blks_per_seg;

  return WriteToDisk(raw_dent_block, data_block_num);
}

zx_status_t MkfsWorker::PurgeNodeChain() {
  FsBlock raw_block;
#ifdef __Fuchsia__
  Node *raw_node = reinterpret_cast<Node *>(raw_block.GetData().data());
#else   // __Fuchsia__
  Node *raw_node = reinterpret_cast<Node *>(raw_block.GetData());
#endif  // __Fuchsia__
  block_t node_segment_block_num = LeToCpu(super_block_.main_blkaddr);
  node_segment_block_num += safemath::checked_cast<uint64_t>(
      params_.cur_seg[static_cast<int>(CursegType::kCursegWarmNode)] * params_.blks_per_seg);

  memset(raw_node, 0xff, sizeof(Node));
  // Purge the 1st block of warm node cur_seg to avoid unnecessary roll-forward recovery.
  return WriteToDisk(raw_block, node_segment_block_num);
}

zx_status_t MkfsWorker::CreateRootDir() {
  std::string err_msg = "Error creating root directory: ";
  if (zx_status_t err = WriteRootInode(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to write root inode " << err;
    return err;
  }
  if (zx_status_t err = PurgeNodeChain(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to purge node chain " << err;
    return err;
  }
  if (zx_status_t err = UpdateNatRoot(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to update NAT for root " << err;
    return err;
  }
  if (zx_status_t err = AddDefaultDentryRoot(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to add default dentries for root " << err;
    return err;
  }
  return ZX_OK;
}

zx_status_t MkfsWorker::TrimDevice() { return bc_->Trim(0, static_cast<block_t>(bc_->Maxblk())); }

zx_status_t MkfsWorker::FormatDevice() {
  if (zx_status_t err = PrepareSuperblock(); err != ZX_OK) {
    return err;
  }

  if (zx_status_t err = TrimDevice(); err != ZX_OK) {
    if (err == ZX_ERR_NOT_SUPPORTED) {
      FX_LOGS(INFO) << "This device doesn't support TRIM";
    } else {
      return err;
    }
  }

  if (zx_status_t err = InitSitArea(); err != ZX_OK) {
    return err;
  }

  if (zx_status_t err = InitNatArea(); err != ZX_OK) {
    return err;
  }

  if (zx_status_t err = CreateRootDir(); err != ZX_OK) {
    return err;
  }

  if (zx_status_t err = WriteCheckPointPack(); err != ZX_OK) {
    return err;
  }

  if (zx_status_t err = WriteSuperblock(); err != ZX_OK) {
    return err;
  }

  // Ensure that all cached data is flushed in the underlying block device
  return bc_->Flush();
}

void PrintUsage() {
  std::cerr << "Usage: mkfs -p \"[OPTIONS]\" devicepath f2fs" << std::endl;
  std::cerr << "[OPTIONS]" << std::endl;
  std::cerr << "  -l label" << std::endl;
  std::cerr << "  -a heap-based allocation [default: 1]" << std::endl;
  std::cerr << "  -o overprovision ratio [default: 5]" << std::endl;
  std::cerr << "  -s # of segments per section [default: 1]" << std::endl;
  std::cerr << "  -z # of sections per zone [default: 1]" << std::endl;
  std::cerr << "  -e [extension list] e.g. \"mp3,gif,mov\"" << std::endl;
  std::cerr << "e.g. mkfs -p \"-l hello -a 1 -o 5 -s 1 -z 1 -e mp3,gif\" devicepath f2fs"
            << std::endl;
}

zx_status_t ParseOptions(int argc, char **argv, MkfsOptions &options) {
  struct option opts[] = {
      {"label", required_argument, nullptr, 'l'},
      {"heap", required_argument, nullptr, 'a'},
      {"op", required_argument, nullptr, 'o'},
      {"seg_per_sec", required_argument, nullptr, 's'},
      {"sec_per_zone", required_argument, nullptr, 'z'},
      {"ext_list", required_argument, nullptr, 'e'},
      {nullptr, 0, nullptr, 0},
  };

  int opt_index = -1;
  int c = -1;

#ifdef __Fuchsia__
  optreset = 1;
#endif  // __Fuchsia__
  optind = 1;

  while ((c = getopt_long(argc, argv, "l:a:o:s:z:e:", opts, &opt_index)) >= 0) {
    switch (c) {
      case 'l':
        options.label = optarg;
        if (options.label.length() >= kVolumeLabelLength) {
          std::cerr << "ERROR: label length should be less than 16." << std::endl;
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case 'a':
        options.heap_based_allocation = (static_cast<uint32_t>(strtoul(optarg, nullptr, 0)) != 0);
        break;
      case 'o':
        options.overprovision_ratio = static_cast<uint32_t>(strtoul(optarg, nullptr, 0));
        if (options.overprovision_ratio == 0) {
          std::cerr << "ERROR: overprovision ratio should be larger than 0." << std::endl;
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case 's':
        options.segs_per_sec = static_cast<uint32_t>(strtoul(optarg, nullptr, 0));
        if (options.segs_per_sec == 0) {
          std::cerr << "ERROR: # of segments per section should be larger than 0." << std::endl;
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case 'z':
        options.secs_per_zone = static_cast<uint32_t>(strtoul(optarg, nullptr, 0));
        if (options.secs_per_zone == 0) {
          std::cerr << "ERROR: # of sections per zone should be larger than 0." << std::endl;
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case 'e':
        options.extension_list = optarg;
        break;
      default:
        PrintUsage();
        return ZX_ERR_INVALID_ARGS;
    };
  };

  return ZX_OK;
}

zx::result<std::unique_ptr<Bcache>> Mkfs(const MkfsOptions &options, std::unique_ptr<Bcache> bc) {
  MkfsWorker mkfs(std::move(bc), options);
  return mkfs.DoMkfs();
}

}  // namespace f2fs
