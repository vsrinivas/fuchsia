// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>

#include <cmath>
#include <codecvt>
#include <iostream>

#include "src/lib/uuid/uuid.h"
#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

MkfsWorker::MkfsWorker(Bcache *bc) : bc_(bc) {}

void MkfsWorker::PrintUsage() {
  fprintf(stderr, "Usage: mkfs -p \"[OPTIONS]\" devicepath f2fs\n");
  fprintf(stderr, "[OPTIONS]\n");
  fprintf(stderr, "  -l label\n");
  fprintf(stderr, "  -a heap-based allocation [default: 1]\n");
  fprintf(stderr, "  -o overprovision ratio [default: 5]\n");
  fprintf(stderr, "  -s # of segments per section [default: 1]\n");
  fprintf(stderr, "  -z # of sections per zone [default: 1]\n");
  fprintf(stderr, "  -e [extension list] e.g. \"mp3,gif,mov\"\n");
  fprintf(stderr, "e.g. mkfs -p \"-l hello -a 1 -o 5 -s 1 -z 1 -e mp3,gif\" devicepath f2fs\n");
}

zx_status_t MkfsWorker::ParseOptions(int argc, char **argv) {
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

  optreset = 1;
  optind = 1;

  while ((c = getopt_long(argc, argv, "l:a:o:s:z:e:", opts, &opt_index)) >= 0) {
    switch (c) {
      case 'l':
        mkfs_options_.label = optarg;
        if (strlen(mkfs_options_.label) >= sizeof(params_.vol_label)) {
          fprintf(stderr, "ERROR: label length should be less than 16.\n");
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case 'a':
        mkfs_options_.heap_based_allocation =
            (static_cast<uint32_t>(strtoul(optarg, nullptr, 0)) != 0);
        break;
      case 'o':
        mkfs_options_.overprovision_ratio = static_cast<uint32_t>(strtoul(optarg, nullptr, 0));
        if (mkfs_options_.overprovision_ratio == 0) {
          fprintf(stderr, "ERROR: overprovision ratio should be larger than 0.\n");
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case 's':
        mkfs_options_.segs_per_sec = static_cast<uint32_t>(strtoul(optarg, nullptr, 0));
        if (mkfs_options_.segs_per_sec == 0) {
          fprintf(stderr, "ERROR: # of segments per section should be larger than 0.\n");
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case 'z':
        mkfs_options_.secs_per_zone = static_cast<uint32_t>(strtoul(optarg, nullptr, 0));
        if (mkfs_options_.secs_per_zone == 0) {
          fprintf(stderr, "ERROR: # of sections per zone should be larger than 0.\n");
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case 'e':
        mkfs_options_.extension_list = optarg;
        break;
      default:
        PrintUsage();
        return ZX_ERR_INVALID_ARGS;
    };
  };

  return ZX_OK;
}

void MkfsWorker::PrintCurrentOption() {
  fprintf(stderr, "f2fs mkfs label = %s\n", mkfs_options_.label);
  fprintf(stderr, "f2fs mkfs heap-based allocation = %d\n", mkfs_options_.heap_based_allocation);
  fprintf(stderr, "f2fs mkfs overprovision ratio = %u\n", mkfs_options_.overprovision_ratio);
  fprintf(stderr, "f2fs mkfs segments per sector = %u\n", mkfs_options_.segs_per_sec);
  fprintf(stderr, "f2fs mkfs sectors per zone = %u\n", mkfs_options_.secs_per_zone);
  fprintf(stderr, "f2fs mkfs extension list = %s\n", mkfs_options_.extension_list);
}

zx_status_t MkfsWorker::DoMkfs() {
#ifdef F2FS_BU_DEBUG
  PrintCurrentOption();
#endif

  InitGlobalParameters();

  if (zx_status_t ret = GetDeviceInfo(); ret != ZX_OK)
    return ret;

  if (zx_status_t ret = FormatDevice(); ret != ZX_OK)
    return ret;
#ifdef F2FS_BU_DEBUG
  FX_LOGS(INFO) << "Formated successfully";
#endif
  return ZX_OK;
}

/*
 * String must be less than 16 characters.
 */
void AsciiToUnicode(const std::string &in_string, std::u16string *out_string) {
  std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> cvt16;

  out_string->assign(cvt16.from_bytes(in_string));
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
  if (mkfs_options_.label != nullptr) {
    memcpy(params_.vol_label, mkfs_options_.label, strlen(mkfs_options_.label) + 1);
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
  fuchsia_hardware_block_BlockInfo info;

  bc_->device()->BlockGetInfo(&info);

  params_.sector_size = info.block_size;
  params_.sectors_per_blk = kBlockSize / info.block_size;
  params_.total_sectors = info.block_count;
  params_.start_sector = kSuperblockStart;

  if (info.block_size < kDefaultSectorSize || info.block_size > kBlockSize) {
    fprintf(stderr, "Error: Block size %d is not supported\n", info.block_size);
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

void MkfsWorker::ConfigureExtensionList() {
  char *ext_str = params_.extension_list;

  super_block_.extension_count = 0;
  memset(super_block_.extension_list, 0, sizeof(super_block_.extension_list));

  int name_len;
  int i = 0;

  for (const char *ext : kMediaExtList) {
    name_len = static_cast<int>(strlen(ext));
    memcpy(super_block_.extension_list[i++], ext, name_len);
  }
  super_block_.extension_count = i;

  if (!ext_str)
    return;

  /* add user ext list */
  char *ue = strtok(ext_str, ",");
  while (ue != nullptr) {
    name_len = static_cast<int>(strlen(ue));
    memcpy(super_block_.extension_list[i++], ue, name_len);
    ue = strtok(nullptr, ",");
    if (i >= kMaxExtension)
      break;
  }

  super_block_.extension_count = i;
}

zx_status_t MkfsWorker::WriteToDisk(void *buf, uint64_t offset, size_t length) {
#ifdef F2FS_BU_DEBUG
  std::cout << std::hex << "writetodeisk: offset= 0x" << offset << " length= 0x" << length
            << std::endl;
#endif

  if (offset % kBlockSize) {
    std::cout << std::hex << "block is not aligned: offset =  " << offset << " length = " << length
              << std::endl;
    return ZX_ERR_INVALID_ARGS;
  }

  if (length % kBlockSize) {
    std::cout << std::hex << "block size is not aligned: offset =  " << offset
              << " length = " << length << std::endl;
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ZX_OK;
  uint64_t curr_offset = offset;

  for (uint64_t i = 0; i < length / kBlockSize; i++) {
    if ((status = bc_->Writeblk(static_cast<block_t>((offset / kBlockSize) + i), buf)) != ZX_OK) {
      std::cout << "mkfs: Failed to write root directory: " << status << std::endl;
    }

    curr_offset += kBlockSize;
  }

  ZX_ASSERT(curr_offset == offset + length);

  return status;
}

zx_status_t MkfsWorker::GetCalculatedOp(uint32_t &op) {
  uint32_t max_op = 0;
  uint32_t max_user_segments = 0;

  if (op < 100 && op > 0)
    return ZX_OK;

  for (uint32_t calc_op = 1; calc_op < 100; calc_op++) {
    uint32_t reserved_segments =
        (2 * (100 / calc_op + 1) + kNrCursegType) * super_block_.segs_per_sec;
    uint32_t user_segments =
        super_block_.segment_count_main -
        ((super_block_.segment_count_main - reserved_segments) * calc_op / 100) - reserved_segments;

    if (user_segments > max_user_segments &&
        (super_block_.segment_count_main - 2) >= reserved_segments) {
      max_user_segments = user_segments;
      max_op = calc_op;
    }
  }
  op = max_op;

  if (max_op == 0)
    return ZX_ERR_INVALID_ARGS;
  return ZX_OK;
}

zx_status_t MkfsWorker::PrepareSuperBlock() {
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

  if (log_sectorsize < 0) {
    printf("\n\tError: Failed to get the sector size: %u!\n", params_.sector_size);
    return ZX_ERR_INVALID_ARGS;
  }

  super_block_.log_sectors_per_block = CpuToLe(log_sectors_per_block);

  if (log_sectors_per_block < 0) {
    printf("\n\tError: Failed to get sectors per block: %u!\n", params_.sectors_per_blk);
    return ZX_ERR_INVALID_ARGS;
  }

  super_block_.log_blocksize = CpuToLe(log_blocksize);
  super_block_.log_blocks_per_seg = CpuToLe(log_blks_per_seg);

  if (log_blks_per_seg < 0) {
    printf("\n\tError: Failed to get block per segment: %u!\n", params_.blks_per_seg);
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
    printf("WARN: Align start sector number in a unit of pages\n");
    printf("\ti.e., start sector: %d, ofs:%d (sectors per page: %d)\n", params_.start_sector,
           params_.start_sector % params_.sectors_per_blk, params_.sectors_per_blk);
  }

  super_block_.segment_count = static_cast<uint32_t>(
      CpuToLe(((params_.total_sectors * params_.sector_size) - zone_align_start_offset) /
              segment_size_bytes));

  super_block_.segment0_blkaddr =
      static_cast<uint32_t>(CpuToLe(zone_align_start_offset / blk_size_bytes));
  super_block_.cp_blkaddr = super_block_.segment0_blkaddr;

  super_block_.segment_count_ckpt = CpuToLe(kNumberOfCheckpointPack);

  super_block_.sit_blkaddr =
      CpuToLe(LeToCpu(super_block_.segment0_blkaddr) +
              (LeToCpu(super_block_.segment_count_ckpt) * (1 << log_blks_per_seg)));

  uint32_t blocks_for_sit =
      (LeToCpu(super_block_.segment_count) + kSitEntryPerBlock - 1) / kSitEntryPerBlock;

  uint32_t sit_segments = (blocks_for_sit + params_.blks_per_seg - 1) / params_.blks_per_seg;

  super_block_.segment_count_sit = CpuToLe(sit_segments * 2);

  super_block_.nat_blkaddr =
      CpuToLe(LeToCpu(super_block_.sit_blkaddr) +
              (LeToCpu(super_block_.segment_count_sit) * params_.blks_per_seg));

  uint32_t total_valid_blks_available =
      (LeToCpu(super_block_.segment_count) -
       (LeToCpu(super_block_.segment_count_ckpt) + LeToCpu(super_block_.segment_count_sit))) *
      params_.blks_per_seg;

  uint32_t blocks_for_nat =
      (total_valid_blks_available + kNatEntryPerBlock - 1) / kNatEntryPerBlock;

  super_block_.segment_count_nat =
      CpuToLe((blocks_for_nat + params_.blks_per_seg - 1) / params_.blks_per_seg);
  /*
   * The number of node segments should not be exceeded a "Threshold".
   * This number resizes NAT bitmap area in a CP page.
   * So the threshold is determined not to overflow one CP page
   */
  uint32_t sit_bitmap_size =
      ((LeToCpu(super_block_.segment_count_sit) / 2) << log_blks_per_seg) / 8;
  uint32_t max_nat_bitmap_size = 4096 - sizeof(Checkpoint) + 1 - sit_bitmap_size;
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

  super_block_.segment_count_ssa =
      CpuToLe((blocks_for_ssa + params_.blks_per_seg - 1) / params_.blks_per_seg);

  uint64_t total_meta_segments =
      LeToCpu(super_block_.segment_count_ckpt) + LeToCpu(super_block_.segment_count_sit) +
      LeToCpu(super_block_.segment_count_nat) + LeToCpu(super_block_.segment_count_ssa);

  if (uint64_t diff = total_meta_segments % (params_.segs_per_sec * params_.secs_per_zone);
      diff != 0)
    super_block_.segment_count_ssa =
        static_cast<uint32_t>(CpuToLe(LeToCpu(super_block_.segment_count_ssa) +
                                      (params_.segs_per_sec * params_.secs_per_zone - diff)));

  super_block_.main_blkaddr =
      CpuToLe(LeToCpu(super_block_.ssa_blkaddr) +
              (LeToCpu(super_block_.segment_count_ssa) * params_.blks_per_seg));

  super_block_.segment_count_main =
      CpuToLe(LeToCpu(super_block_.segment_count) -
              (LeToCpu(super_block_.segment_count_ckpt) + LeToCpu(super_block_.segment_count_sit) +
               LeToCpu(super_block_.segment_count_nat) + LeToCpu(super_block_.segment_count_ssa)));

  super_block_.section_count =
      CpuToLe(LeToCpu(super_block_.segment_count_main) / params_.segs_per_sec);

  super_block_.segment_count_main =
      CpuToLe(LeToCpu(super_block_.section_count) * params_.segs_per_sec);

  if (zx_status_t status = GetCalculatedOp(params_.overprovision); status != ZX_OK) {
    FX_LOGS(WARNING) << "Error: Device size is not sufficient for F2FS volume";
    return ZX_ERR_NO_SPACE;
  }

  // The number of reserved_segments depends on the OP value. Assuming OP is 20%, 20% of a dirty
  // segment is invalid. That is, running GC on 5 dirty segments can obtain one free segment.
  // Therefore, the required reserved_segments can be obtained with 100 / OP.
  // If the data page is moved to another segment due to GC, the dnode that refers to it should be
  // modified. This requires twice the reserved_segments.
  // Current active segments have the next segment in advance, of which require 6 additional
  // segments.
  params_.reserved_segments =
      (2 * (100 / params_.overprovision + 1) + kNrCursegType) * params_.segs_per_sec;

  if ((LeToCpu(super_block_.segment_count_main) - 2) < params_.reserved_segments) {
    printf("Error: Device size is not sufficient for F2FS volume, more segment needed =%u",
           params_.reserved_segments - (LeToCpu(super_block_.segment_count_main) - 2));
    return ZX_ERR_NO_SPACE;
  }

  memcpy(super_block_.uuid, uuid::Uuid::Generate().bytes(), 16);

  std::string vol_label(reinterpret_cast<char const *>(params_.vol_label));
  std::u16string volume_name;

  AsciiToUnicode(vol_label, &volume_name);

  volume_name.copy(reinterpret_cast<char16_t *>(super_block_.volume_name), vol_label.size());
  super_block_.volume_name[vol_label.size()] = '\0';

  super_block_.node_ino = CpuToLe(uint32_t{1});
  super_block_.meta_ino = CpuToLe(uint32_t{2});
  super_block_.root_ino = CpuToLe(uint32_t{3});

  uint32_t total_zones = ((LeToCpu(super_block_.segment_count_main) - 1) / params_.segs_per_sec) /
                         params_.secs_per_zone;
  if (total_zones <= 6) {
    printf("\n\tError: %d zones: Need more zones	by shrinking zone size\n", total_zones);
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
  uint32_t blk_size_bytes = 1 << LeToCpu(super_block_.log_blocksize);
  uint32_t seg_size_bytes = (1 << LeToCpu(super_block_.log_blocks_per_seg)) * blk_size_bytes;

  uint8_t *zero_buf = static_cast<uint8_t *>(calloc(sizeof(uint8_t), seg_size_bytes));
  if (zero_buf == nullptr) {
    printf("\n\tError: Calloc Failed for sit_zero_buf!!!\n");
    return ZX_ERR_NO_MEMORY;
  }

  uint64_t sit_seg_blk_offset = LeToCpu(super_block_.sit_blkaddr) * blk_size_bytes;

  for (uint32_t index = 0; index < (LeToCpu(super_block_.segment_count_sit) / 2); index++) {
    if (zx_status_t ret = WriteToDisk(zero_buf, sit_seg_blk_offset, seg_size_bytes); ret != ZX_OK) {
      printf("\n\tError: While zeroing out the sit area on disk!!!\n");
      return ret;
    }
    sit_seg_blk_offset = sit_seg_blk_offset + seg_size_bytes;
  }

  free(zero_buf);
  return ZX_OK;
}

zx_status_t MkfsWorker::InitNatArea() {
  uint64_t blk_size_bytes = 1 << LeToCpu(super_block_.log_blocksize);
  uint64_t seg_size_bytes = (1 << LeToCpu(super_block_.log_blocks_per_seg)) * blk_size_bytes;

  uint8_t *nat_buf = static_cast<uint8_t *>(calloc(sizeof(uint8_t), seg_size_bytes));
  if (nat_buf == nullptr) {
    printf("\n\tError: Calloc Failed for nat_zero_blk!!!\n");
    return ZX_ERR_NO_MEMORY;
  }

  uint64_t nat_seg_blk_offset = LeToCpu(super_block_.nat_blkaddr) * blk_size_bytes;

  for (uint32_t index = 0; index < (LeToCpu(super_block_.segment_count_nat) / 2); index++) {
    if (zx_status_t ret = WriteToDisk(nat_buf, nat_seg_blk_offset, seg_size_bytes); ret != ZX_OK) {
      printf("\n\tError: While zeroing out the nat area on disk!!!\n");
      return ret;
    }
    nat_seg_blk_offset = nat_seg_blk_offset + (2 * seg_size_bytes);
  }

  free(nat_buf);
  return ZX_OK;
}

zx_status_t MkfsWorker::WriteCheckPointPack() {
  Checkpoint *ckp = static_cast<Checkpoint *>(calloc(kBlockSize, 1));
  if (ckp == nullptr) {
    printf("\n\tError: Calloc Failed for Checkpoint!!!\n");
    return ZX_ERR_NO_MEMORY;
  }

  SummaryBlock *sum = static_cast<SummaryBlock *>(calloc(kBlockSize, 1));
  if (sum == nullptr) {
    printf("\n\tError: Calloc Failed for summay_node!!!\n");
    return ZX_ERR_NO_MEMORY;
  }

  /* 1. cp page 1 of checkpoint pack 1 */
  ckp->checkpoint_ver = 1;
  ckp->cur_node_segno[0] = CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)]);
  ckp->cur_node_segno[1] = CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegWarmNode)]);
  ckp->cur_node_segno[2] = CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegColdNode)]);
  ckp->cur_data_segno[0] = CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegHotData)]);
  ckp->cur_data_segno[1] = CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegWarmData)]);
  ckp->cur_data_segno[2] = CpuToLe(params_.cur_seg[static_cast<int>(CursegType::kCursegColdData)]);
  for (int i = 3; i < kMaxActiveNodeLogs; i++) {
    ckp->cur_node_segno[i] = 0xffffffff;
    ckp->cur_data_segno[i] = 0xffffffff;
  }

  ckp->cur_node_blkoff[0] = CpuToLe(uint16_t{1});
  ckp->cur_data_blkoff[0] = CpuToLe(uint16_t{1});
  ckp->valid_block_count = CpuToLe(uint64_t{2});
  ckp->rsvd_segment_count = CpuToLe(params_.reserved_segments);
  ckp->overprov_segment_count =
      CpuToLe((LeToCpu(super_block_.segment_count_main) - LeToCpu(ckp->rsvd_segment_count)) *
              params_.overprovision / 100);
  ckp->overprov_segment_count =
      CpuToLe(LeToCpu(ckp->overprov_segment_count) + LeToCpu(ckp->rsvd_segment_count));

  /* main segments - reserved segments - (node + data segments) */
  ckp->free_segment_count = CpuToLe(LeToCpu(super_block_.segment_count_main) - 6);

  ckp->user_block_count =
      CpuToLe(((LeToCpu(ckp->free_segment_count) + 6 - LeToCpu(ckp->overprov_segment_count)) *
               params_.blks_per_seg));

  ckp->cp_pack_total_block_count = CpuToLe(uint32_t{8});
  ckp->ckpt_flags |= kCpUmountFlag;
  ckp->cp_pack_start_sum = CpuToLe(uint32_t{1});
  ckp->valid_node_count = CpuToLe(uint32_t{1});
  ckp->valid_inode_count = CpuToLe(uint32_t{1});
  ckp->next_free_nid = CpuToLe(LeToCpu(super_block_.root_ino) + 1);

  ckp->sit_ver_bitmap_bytesize = CpuToLe(
      ((LeToCpu(super_block_.segment_count_sit) / 2) << LeToCpu(super_block_.log_blocks_per_seg)) /
      8);

  ckp->nat_ver_bitmap_bytesize = CpuToLe(
      ((LeToCpu(super_block_.segment_count_nat) / 2) << LeToCpu(super_block_.log_blocks_per_seg)) /
      8);

  ckp->checksum_offset = CpuToLe(uint32_t{kChecksumOffset});

  uint32_t crc = F2fsCalCrc32(kF2fsSuperMagic, ckp, LeToCpu(ckp->checksum_offset));
  *(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(ckp) +
                                 LeToCpu(ckp->checksum_offset))) = crc;

  uint64_t blk_size_bytes = 1 << LeToCpu(super_block_.log_blocksize);
  uint64_t cp_seg_blk_offset = LeToCpu(super_block_.segment0_blkaddr) * blk_size_bytes;

  if (zx_status_t ret = WriteToDisk(ckp, cp_seg_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the ckp to disk!!!\n");
    return ret;
  }

  /* 2. Prepare and write Segment summary for data blocks */
  memset(sum, 0, sizeof(SummaryBlock));
  SetSumType((&sum->footer), kSumTypeData);

  sum->entries[0].nid = super_block_.root_ino;
  sum->entries[0].ofs_in_node = 0;

  cp_seg_blk_offset += blk_size_bytes;
  if (zx_status_t ret = WriteToDisk(sum, cp_seg_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the sum_blk to disk!!!\n");
    return ret;
  }

  /* 3. Fill segment summary for data block to zero. */
  memset(sum, 0, sizeof(SummaryBlock));
  SetSumType((&sum->footer), kSumTypeData);

  cp_seg_blk_offset += blk_size_bytes;
  if (zx_status_t ret = WriteToDisk(sum, cp_seg_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the sum_blk to disk!!!\n");
    return ret;
  }

  /* 4. Fill segment summary for data block to zero. */
  memset(sum, 0, sizeof(SummaryBlock));
  SetSumType((&sum->footer), kSumTypeData);

  /* inode sit for root */
  sum->n_sits = CpuToLe(uint16_t{6});
  sum->sit_j.entries[0].segno = ckp->cur_node_segno[0];
  sum->sit_j.entries[0].se.vblocks =
      CpuToLe(uint16_t{(static_cast<int>(CursegType::kCursegHotNode) << 10) | 1});
  SetValidBitmap(0, sum->sit_j.entries[0].se.valid_map);
  sum->sit_j.entries[1].segno = ckp->cur_node_segno[1];
  sum->sit_j.entries[1].se.vblocks =
      CpuToLe(uint16_t{(static_cast<int>(CursegType::kCursegWarmNode) << 10)});
  sum->sit_j.entries[2].segno = ckp->cur_node_segno[2];
  sum->sit_j.entries[2].se.vblocks =
      CpuToLe(uint16_t{(static_cast<int>(CursegType::kCursegColdNode) << 10)});

  /* data sit for root */
  sum->sit_j.entries[3].segno = ckp->cur_data_segno[0];
  sum->sit_j.entries[3].se.vblocks =
      CpuToLe(uint16_t{(static_cast<uint16_t>(CursegType::kCursegHotData) << 10) | 1});
  SetValidBitmap(0, sum->sit_j.entries[3].se.valid_map);
  sum->sit_j.entries[4].segno = ckp->cur_data_segno[1];
  sum->sit_j.entries[4].se.vblocks =
      CpuToLe(uint16_t{(static_cast<int>(CursegType::kCursegWarmData) << 10)});
  sum->sit_j.entries[5].segno = ckp->cur_data_segno[2];
  sum->sit_j.entries[5].se.vblocks =
      CpuToLe(uint16_t{(static_cast<int>(CursegType::kCursegColdData) << 10)});

  cp_seg_blk_offset += blk_size_bytes;
  if (zx_status_t ret = WriteToDisk(sum, cp_seg_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the sum_blk to disk!!!\n");
    return ret;
  }

  /* 5. Prepare and write Segment summary for node blocks */
  memset(sum, 0, sizeof(SummaryBlock));
  SetSumType((&sum->footer), kSumTypeNode);

  sum->entries[0].nid = super_block_.root_ino;
  sum->entries[0].ofs_in_node = 0;

  cp_seg_blk_offset += blk_size_bytes;
  if (zx_status_t ret = WriteToDisk(sum, cp_seg_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the sum_blk to disk!!!\n");
    return ret;
  }

  /* 6. Fill segment summary for data block to zero. */
  memset(sum, 0, sizeof(SummaryBlock));
  SetSumType((&sum->footer), kSumTypeNode);

  cp_seg_blk_offset += blk_size_bytes;
  if (zx_status_t ret = WriteToDisk(sum, cp_seg_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the sum_blk to disk!!!\n");
    return ret;
  }

  /* 7. Fill segment summary for data block to zero. */
  memset(sum, 0, sizeof(SummaryBlock));
  SetSumType((&sum->footer), kSumTypeNode);
  cp_seg_blk_offset += blk_size_bytes;
  if (zx_status_t ret = WriteToDisk(sum, cp_seg_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the sum_blk to disk!!!\n");
    return ret;
  }

  /* 8. cp page2 */
  cp_seg_blk_offset += blk_size_bytes;
  if (zx_status_t ret = WriteToDisk(ckp, cp_seg_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the ckp to disk!!!\n");
    return ret;
  }

  /* 9. cp page 1 of check point pack 2
   * Initiatialize other checkpoint pack with version zero
   */
  ckp->checkpoint_ver = 0;

  crc = F2fsCalCrc32(kF2fsSuperMagic, ckp, LeToCpu(ckp->checksum_offset));
  *(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(ckp) +
                                 LeToCpu(ckp->checksum_offset))) = crc;

  cp_seg_blk_offset =
      (LeToCpu(super_block_.segment0_blkaddr) + params_.blks_per_seg) * blk_size_bytes;
  if (zx_status_t ret = WriteToDisk(ckp, cp_seg_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the ckp to disk!!!\n");
    return ret;
  }

  free(sum);
  free(ckp);
  return ZX_OK;
}

zx_status_t MkfsWorker::WriteSuperBlock() {
  uint8_t *zero_buff = static_cast<uint8_t *>(calloc(kBlockSize, 1));

  memcpy(zero_buff + kSuperOffset, &super_block_, sizeof(super_block_));

  for (uint64_t index = 0; index < 2; index++) {
    if (zx_status_t ret = WriteToDisk(zero_buff, index * kBlockSize, kBlockSize); ret != ZX_OK) {
      printf("\n\tError: While while writing supe_blk	on disk!!! index : %lu\n", index);
      return ret;
    }
  }

  free(zero_buff);
  return ZX_OK;
}

zx_status_t MkfsWorker::WriteRootInode() {
  Node *raw_node = static_cast<Node *>(calloc(kBlockSize, 1));
  if (raw_node == nullptr) {
    printf("\n\tError: Calloc Failed for raw_node!!!\n");
    return ZX_ERR_NO_MEMORY;
  }

  raw_node->footer.nid = super_block_.root_ino;
  raw_node->footer.ino = super_block_.root_ino;
  raw_node->footer.cp_ver = CpuToLe(uint64_t{1});
  raw_node->footer.next_blkaddr = CpuToLe(
      LeToCpu(super_block_.main_blkaddr) +
      params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)] * params_.blks_per_seg + 1);

  raw_node->i.i_mode = CpuToLe(uint16_t{0x41ed});
  raw_node->i.i_links = CpuToLe(uint32_t{2});
  raw_node->i.i_uid = CpuToLe(getuid());
  raw_node->i.i_gid = CpuToLe(getgid());

  uint64_t blk_size_bytes = 1 << LeToCpu(super_block_.log_blocksize);
  raw_node->i.i_size = CpuToLe(1 * blk_size_bytes); /* dentry */
  raw_node->i.i_blocks = CpuToLe(uint64_t{2});

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
  raw_node->i.i_current_depth = CpuToLe(uint32_t{1});

  uint64_t data_blk_nor =
      LeToCpu(super_block_.main_blkaddr) +
      params_.cur_seg[static_cast<int>(CursegType::kCursegHotData)] * params_.blks_per_seg;
  raw_node->i.i_addr[0] = static_cast<uint32_t>(CpuToLe(data_blk_nor));

  raw_node->i.i_ext.fofs = 0;
  raw_node->i.i_ext.blk_addr = static_cast<uint32_t>(CpuToLe(data_blk_nor));
  raw_node->i.i_ext.len = CpuToLe(uint32_t{1});

  uint64_t main_area_node_seg_blk_offset = LeToCpu(super_block_.main_blkaddr);
  main_area_node_seg_blk_offset +=
      params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)] * params_.blks_per_seg;
  main_area_node_seg_blk_offset *= blk_size_bytes;

  if (zx_status_t ret = WriteToDisk(raw_node, main_area_node_seg_blk_offset, kBlockSize);
      ret != ZX_OK) {
    printf("\n\tError: While writing the raw_node to disk!!!, size = %lu\n", sizeof(Node));
    return ret;
  }

  memset(raw_node, 0xff, sizeof(Node));

  if (zx_status_t ret = WriteToDisk(raw_node, main_area_node_seg_blk_offset + 4096, kBlockSize);
      ret != ZX_OK) {
    printf("\n\tError: While writing the raw_node to disk!!!\n");
    return ret;
  }
  free(raw_node);
  return ZX_OK;
}

zx_status_t MkfsWorker::UpdateNatRoot() {
  NatBlock *nat_blk = static_cast<NatBlock *>(calloc(kBlockSize, 1));
  if (nat_blk == nullptr) {
    printf("\n\tError: Calloc Failed for nat_blk!!!\n");
    return ZX_ERR_NO_MEMORY;
  }

  /* update root */
  nat_blk->entries[super_block_.root_ino].block_addr =
      CpuToLe(LeToCpu(super_block_.main_blkaddr) +
              params_.cur_seg[static_cast<int>(CursegType::kCursegHotNode)] * params_.blks_per_seg);
  nat_blk->entries[super_block_.root_ino].ino = super_block_.root_ino;

  /* update node nat */
  nat_blk->entries[super_block_.node_ino].block_addr = CpuToLe(uint32_t{1});
  nat_blk->entries[super_block_.node_ino].ino = super_block_.node_ino;

  /* update meta nat */
  nat_blk->entries[super_block_.meta_ino].block_addr = CpuToLe(uint32_t{1});
  nat_blk->entries[super_block_.meta_ino].ino = super_block_.meta_ino;

  uint64_t blk_size_bytes = 1 << LeToCpu(super_block_.log_blocksize);

  uint64_t nat_seg_blk_offset = LeToCpu(super_block_.nat_blkaddr) * blk_size_bytes;

  if (zx_status_t ret = WriteToDisk(nat_blk, nat_seg_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the nat_blk set0 to disk!!!\n");
    return ret;
  }

  free(nat_blk);
  return ZX_OK;
}

zx_status_t MkfsWorker::AddDefaultDentryRoot() {
  DentryBlock *dent_blk = static_cast<DentryBlock *>(calloc(kBlockSize, 1));
  if (dent_blk == nullptr) {
    printf("\n\tError: Calloc Failed for dent_blk!!!\n");
    return ZX_ERR_NO_MEMORY;
  }

  dent_blk->dentry[0].hash_code = 0;
  dent_blk->dentry[0].ino = super_block_.root_ino;
  dent_blk->dentry[0].name_len = CpuToLe(uint16_t{1});
  dent_blk->dentry[0].file_type = static_cast<uint8_t>(FileType::kFtDir);
  memcpy(dent_blk->filename[0], ".", 1);

  dent_blk->dentry[1].hash_code = 0;
  dent_blk->dentry[1].ino = super_block_.root_ino;
  dent_blk->dentry[1].name_len = CpuToLe(uint16_t{2});
  dent_blk->dentry[1].file_type = static_cast<uint8_t>(FileType::kFtDir);
  memcpy(dent_blk->filename[1], "..", 2);

  /* bitmap for . and .. */
  dent_blk->dentry_bitmap[0] = (1 << 1) | (1 << 0);
  uint64_t blk_size_bytes = 1 << LeToCpu(super_block_.log_blocksize);
  uint64_t data_blk_offset =
      (LeToCpu(super_block_.main_blkaddr) +
       params_.cur_seg[static_cast<int>(CursegType::kCursegHotData)] * params_.blks_per_seg) *
      blk_size_bytes;

  if (zx_status_t ret = WriteToDisk(dent_blk, data_blk_offset, kBlockSize); ret != ZX_OK) {
    printf("\n\tError: While writing the dentry_blk to disk!!!\n");
    return ret;
  }

  free(dent_blk);
  return ZX_OK;
}

zx_status_t MkfsWorker::CreateRootDir() {
  zx_status_t err = ZX_OK;
  const char err_msg[] = "Error creating root directry: ";
  if (err = WriteRootInode(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to write root inode" << err;
    return err;
  }
  if (err = UpdateNatRoot(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to update NAT for root" << err;
    return err;
  }
  if (err = AddDefaultDentryRoot(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to add default dentries for root" << err;
    return err;
  }
  return err;
}

zx_status_t MkfsWorker::TrimDevice() {
  return bc_->Trim(0, static_cast<block_t>(params_.total_sectors));
}

zx_status_t MkfsWorker::FormatDevice() {
  zx_status_t err = ZX_OK;
  const char err_msg[] = "Error formatting the device: ";
  if (err = PrepareSuperBlock(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to prepare superblock information";
    return err;
  }

  if (err = TrimDevice(); err != ZX_OK) {
    if (err == ZX_ERR_NOT_SUPPORTED) {
      FX_LOGS(INFO) << "This device doesn't support TRIM";
    } else {
      FX_LOGS(ERROR) << err_msg << "Failed to trim whole device" << err;
      return err;
      ;
    }
  }

  if (err = InitSitArea(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to Initialise the SIT AREA" << err;
    return err;
  }

  if (err = InitNatArea(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to Initialise the NAT AREA" << err;
    return err;
  }

  if (err = CreateRootDir(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to create the root directory" << err;
    return err;
  }

  if (err = WriteCheckPointPack(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to write the check point pack" << err;
    return err;
  }

  if (err = WriteSuperBlock(); err != ZX_OK) {
    FX_LOGS(ERROR) << err_msg << "Failed to write the Super Block" << err;
    return err;
  }

  // Ensure that all cached data is flushed in the underlying block device
  bc_->Flush();
  return err;
}

zx_status_t Mkfs(Bcache *bc, int argc, char **argv) {
  MkfsWorker mkfs(bc);

  if (zx_status_t ret = mkfs.ParseOptions(argc, argv); ret != ZX_OK) {
    return ret;
  }

  return mkfs.DoMkfs();
}

}  // namespace f2fs
