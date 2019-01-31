// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ndmp.h"

#if INC_NDM
#include <kprivate/fsprivate.h>

// Global Function Definitions

#if INC_FTL_NDM && INC_SECT_FTL
// ndmWrFatPartition: Write Master Boot Record to NDM FAT partition
//
//      Inputs: ndm = pointer to NDM control block
//              part_num = NDM partition number
//
//     Returns: 0 on success, -1 on error
//
int ndmWrFatPartition(NDM ndm, ui32 part_num) {
    FtlNdmVol ftl;
    FatVol fat;
    void* ftl_ndm;

    // Assign user configurable parameters for FAT/FTL volume.
    ftl.flags = 0;
    ftl.cached_map_pages = 1;
    fat.flags = 0;

    // Specify the FAT type and cluster size used by format() calls.
    fat.desired_sects_per_clust = 0; // cluster size
    fat.desired_type = FATANY;       // FAT type

    // Add an FTL to this partition.
    ftl_ndm = ndmAddFatFTL(ndm, part_num, &ftl, &fat);
    if (ftl_ndm == NULL)
        return -1;

    // Write Master Boot Record with 1 partition to media.
    if (FatWrPartition(&fat))
        return -1;

    // Remove FTL from this partition and return success.
    FtlnFreeFTL(ftl_ndm);
    return 0;
}
#endif // INC_FTL_NDM && INC_SECT_FTL

//   ndmDelVol: Un-initialize a Blunk file system volume, or custom
//              one, for a partition entry in the partition table
//
//      Inputs: ndm = pointer to NDM control block
//              part_num = partition number
//
//     Returns: 0 on success, -1 on error
//
int ndmDelVol(CNDM ndm, ui32 part_num) {
    const NDMPartition* part;

    // Get handle to entry in partitions table. Return if error.
    part = ndmGetPartition(ndm, part_num);
    if (part == NULL)
        return -1;

    // Based on partition type, either perform Blunk un-initialization,
    // or custom one if present.
    switch (part->type) {
#if INC_FFS_NDM
        case FFS_VOL:
            // Remove partition's TargetFFS volume. Return status.
            return FfsDelVol(part->name);
#endif

#if INC_FTL_NDM_MLC || INC_FTL_NDM_SLC
        case FAT_VOL:
        case XFS_VOL:
            // Remove partition's FTL and FAT or XFS volume. Return status.
            return FtlNdmDelVol(part->name);
#endif


            // This is where additional custom type cases could be added
    }

    // Return error if partition type is not handled.
    return -1;
}

//  ndmDelVols: Loop through partition table un-initializing valid
//              partitions
//
//       Input: ndm = pointer to NDM control block
//
//     Returns: 0 on success, -1 on failure
//
int ndmDelVols(CNDM ndm) {
    ui32 i, num_partitions;
    int status = 0;

    // Get the total number of partitions.
    num_partitions = ndmGetNumPartitions(ndm);

    // Loop through all partitions, un-initializing valid ones.
    for (i = 0; i < num_partitions; ++i)
        if (ndmDelVol(ndm, i))
            status = -1;

    // Return status.
    return status;
}
#endif // INC_NDM
