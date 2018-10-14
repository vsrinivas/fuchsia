// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <fbl/array.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fvm/fvm.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/fifo.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zxcpp/new.h>

namespace {

constexpr char kUsageMessage[] = R"""(
Validate the metadata of a FVM using a saved image file (or block device).

fvm-check [options] image_file

Options:
  --block-size (-b) xxx : Number of bytes per block. Defaults to 512.
  --silent (-s): Silences all stdout logging info. Defaults to false.
)""";

class Logger {
public:
    explicit Logger(bool silent) : silent_(silent) {};

    void SetSilent(bool silent) {
        silent_ = silent;
    }

    // Prints the format string and arguments to stdout, unless explicitly silenced.
    void Log(const char* format, ...) const {
        va_list arg;
        if (!silent_) {
            va_start(arg, format);
            vprintf(format, arg);
            va_end(arg);
        }
    }

private:
    bool silent_;
};

struct Config {
    const char* path;
    uint32_t block_size;
    Logger logger;
};

bool GetOptions(int argc, char** argv, Config* config) {
    while (true) {
        struct option options[] = {
            {"block-size", required_argument, nullptr, 'b'},
            {"silent", no_argument, nullptr, 's'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int opt_index;
        int c = getopt_long(argc, argv, "b:sh", options, &opt_index);
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'b':
            config->block_size = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
            break;
        case 's':
            config->logger.SetSilent(true);
            break;
        case 'h':
            return false;
        }
    }
    if (argc == optind + 1) {
        config->path = argv[optind];
        return true;
    }
    return false;
}

bool ValidateOptions(const Config& config) {
    if (!config.path) {
        fprintf(stderr, "Input path needed\n");
        fprintf(stderr, "%s\n", kUsageMessage);
        return false;
    }
    if (config.block_size == 0) {
        fprintf(stderr, "Invalid block size\n");
        return false;
    }
    return true;
}

// Cached information from loading and validating the FVM.
struct FvmInfo {
    // Contains both copies of metadata.
    fbl::Array<uint8_t> metadata;
    size_t valid_metadata_offset;
    const uint8_t* valid_metadata;
    const uint8_t* invalid_metadata;
    size_t block_size;
    size_t block_count;
    size_t device_size;
    size_t slice_size;
};

// Parses the FVM info from the device, and validate it (minimally).
bool LoadFVM(const Config& config, FvmInfo* out) {
    fbl::unique_fd fd(open(config.path, O_RDONLY));
    if (!fd) {
        fprintf(stderr, "Cannot open %s\n", config.path);
        return false;
    }

    const off_t device_size = lseek(fd.get(), 0, SEEK_END);
    if (device_size < 0) {
        fprintf(stderr, "Unable to get file length\n");
        return false;
    }
    if (device_size % config.block_size != 0) {
        fprintf(stderr, "File size is not divisible by block size\n");
        return false;
    }
    const size_t block_count = device_size / config.block_size;

    fbl::unique_ptr<uint8_t[]> header(new uint8_t[FVM_BLOCK_SIZE]);
    if (pread(fd.get(), header.get(), FVM_BLOCK_SIZE, 0) != static_cast<ssize_t>(FVM_BLOCK_SIZE)) {
        fprintf(stderr, "Could not read header\n");
        return false;
    }
    const fvm::fvm_t* superblock = reinterpret_cast<fvm::fvm_t*>(header.get());
    const size_t slice_size = superblock->slice_size;
    if (slice_size % config.block_size != 0) {
        fprintf(stderr, "Slice size not divisible by block size\n");
        return false;
    }
    const size_t metadata_size = fvm::MetadataSize(device_size, slice_size);
    fbl::unique_ptr<uint8_t[]> metadata(new uint8_t[metadata_size * 2]);
    if (pread(fd.get(), metadata.get(), metadata_size * 2, 0) !=
        static_cast<ssize_t>(metadata_size * 2)) {
        fprintf(stderr, "Could not read metadata\n");
        return false;
    }

    const void* metadata1 = metadata.get();
    const void* metadata2 = reinterpret_cast<const void*>(metadata.get() + metadata_size);

    const void* valid_metadata;
    zx_status_t status = fvm_validate_header(metadata1, metadata2, metadata_size,
                                             &valid_metadata);
    if (status != ZX_OK) {
        fprintf(stderr, "Invalid FVM metadata\n");
        return false;
    }

    const void* invalid_metadata = (metadata1 == valid_metadata) ? metadata2 : metadata1;
    const size_t valid_metadata_offset = (metadata1 == valid_metadata) ? 0 : metadata_size;

    FvmInfo info = {
        fbl::Array<uint8_t>(metadata.release(), metadata_size * 2),
        valid_metadata_offset,
        static_cast<const uint8_t*>(valid_metadata),
        static_cast<const uint8_t*>(invalid_metadata),
        config.block_size,
        block_count,
        static_cast<size_t>(device_size),
        slice_size,
    };

    *out = fbl::move(info);
    return true;
}

struct Slice {
    uint64_t virtual_partition;
    uint64_t virtual_slice;
    uint64_t physical_slice;
};

struct Partition {
    bool Allocated() const { return entry != nullptr; }

    const fvm::vpart_entry_t* entry = nullptr;
    fbl::Vector<Slice> slices;
};

// Acquires a list of slices and partitions while parsing the FVM.
//
// Returns false if the FVM contains contradictory or invalid data.
bool LoadPartitions(const size_t slice_count, const fvm::slice_entry_t* slice_table,
                    const fvm::vpart_entry_t* vpart_table,
                    fbl::Vector<Slice>* out_slices, fbl::Array<Partition>* out_partitions) {
    fbl::Vector<Slice> slices;
    fbl::Array<Partition> partitions(new Partition[FVM_MAX_ENTRIES], FVM_MAX_ENTRIES);

    bool valid = true;

    // Initialize all allocated partitions.
    for (size_t i = 1; i < FVM_MAX_ENTRIES; i++) {
        const uint32_t slices = vpart_table[i].slices;
        if (slices != 0) {
            partitions[i].entry = &vpart_table[i];
        }
    }

    // Initialize all slices, ensure they are used for allocated partitions.
    for (size_t i = 1; i <= slice_count; i++) {
        if (slice_table[i].Vpart() != FVM_SLICE_ENTRY_FREE) {
            const uint64_t vpart = slice_table[i].Vpart();
            if (vpart >= FVM_MAX_ENTRIES) {
                fprintf(stderr, "Invalid vslice entry; claims vpart which is out of range.\n");
                valid = false;
            } else if (!partitions[vpart].Allocated()) {
                fprintf(stderr, "Invalid slice entry; claims that it is allocated to invalid ");
                fprintf(stderr, "partition %zu\n", vpart);
                valid = false;
            }

            Slice slice = { vpart, slice_table[i].Vslice(), i };

            slices.push_back(slice);
            partitions[vpart].slices.push_back(fbl::move(slice));
        }
    }

    // Validate that all allocated partitions are correct about the number of slices used.
    for (size_t i = 1; i < FVM_MAX_ENTRIES; i++) {
        if (partitions[i].Allocated()) {
            const size_t claimed = partitions[i].entry->slices;
            const size_t actual = partitions[i].slices.size();
            if (claimed != actual) {
                fprintf(stderr, "Disagreement about allocated slice count: ");
                fprintf(stderr, "Partition %zu claims %zu slices, has %zu\n", i, claimed, actual);
                valid = false;
            }
        }
    }

    *out_slices = fbl::move(slices);
    *out_partitions = fbl::move(partitions);
    return valid;
}

// Displays information about |slices|, assuming they are sorted in physical slice order.
void DumpSlices(const Config& config, const fbl::Vector<Slice>& slices) {
    config.logger.Log("[  Slice Info  ]\n");
    Slice* run_start = nullptr;
    size_t run_length = 0;

    // Prints whatever information we can from the current contiguous range of
    // virtual / physical slices, then reset the "run" information.
    //
    // A run is a contiguous set of virtual / physical slices, all allocated to the same
    // virtual partition. Noncontiguity in either the virtual or physical range
    // "breaks" the run, since these cases provide new information.
    auto start_run = [&run_start, &run_length](Slice* slice) {
        run_start = slice;
        run_length = 1;
    };
    auto end_run = [&config, &run_start, &run_length]() {
        if (run_length == 1) {
            config.logger.Log("Physical Slice %zu allocated\n", run_start->physical_slice);
            config.logger.Log("  Allocated as virtual slice %zu\n", run_start->virtual_slice);
            config.logger.Log("  Allocated to partition %zu\n", run_start->virtual_partition);
        } else if (run_length > 1) {
            config.logger.Log("Physical Slices [%zu, %zu] allocated\n",
                   run_start->physical_slice, run_start->physical_slice + run_length - 1);
            config.logger.Log("  Allocated as virtual slices [%zu, %zu]\n",
                   run_start->virtual_slice, run_start->virtual_slice + run_length - 1);
            config.logger.Log("  Allocated to partition %zu\n", run_start->virtual_partition);
        }
        run_start = nullptr;
        run_length = 0;
    };

    if (!slices.is_empty()) {
        start_run(&slices[0]);
    }
    for (size_t i = 1; i < slices.size(); i++) {
        const auto& slice = slices[i];
        const size_t expected_pslice = run_start->physical_slice + run_length;
        const size_t expected_vslice = run_start->virtual_slice + run_length;
        if (slice.physical_slice == expected_pslice &&
            slice.virtual_slice == expected_vslice &&
            slice.virtual_partition == run_start->virtual_partition) {
            run_length++;
        } else {
            end_run();
            start_run(&slices[i]);
        }
    }
    end_run();
}

// Outputs and checks information about the FVM, optionally logging to stdout.
bool CheckFVM(const Config& config, const FvmInfo& info) {
    auto superblock = reinterpret_cast<const fvm::fvm_t*>(info.valid_metadata);
    auto invalid_superblock = reinterpret_cast<const fvm::fvm_t*>(info.invalid_metadata);
    config.logger.Log("[  FVM Info  ]\n");
    config.logger.Log("Version: %" PRIu64 "\n", superblock->version);
    config.logger.Log("Generation number: %" PRIu64 "\n", superblock->generation);
    config.logger.Log("Generation number: %" PRIu64 " (invalid copy)\n", invalid_superblock->generation);
    config.logger.Log("\n");

    const size_t slice_count = fvm::UsableSlicesCount(info.device_size, info.slice_size);
    config.logger.Log("[  Size Info  ]\n");
    config.logger.Log("Device Length: %zu\n", info.device_size);
    config.logger.Log("   Block size: %zu\n", info.block_size);
    config.logger.Log("   Slice size: %zu\n", info.slice_size);
    config.logger.Log("  Slice count: %zu\n", slice_count);
    config.logger.Log("\n");

    const size_t metadata_size = fvm::MetadataSize(info.device_size, info.slice_size);
    const size_t metadata_count = 2;
    const size_t metadata_end = metadata_size * metadata_count;
    config.logger.Log("[  Metadata  ]\n");
    config.logger.Log("Valid metadata start: 0x%016zx\n", info.valid_metadata_offset);
    config.logger.Log("      Metadata start: 0x%016x\n", 0);
    config.logger.Log("       Metadata size: %zu (for each copy)\n", metadata_size);
    config.logger.Log("      Metadata count: %zu\n", metadata_count);
    config.logger.Log("        Metadata end: 0x%016zx\n", metadata_end);
    config.logger.Log("\n");

    config.logger.Log("[  All Subsequent Offsets Relative to Valid Metadata Start  ]\n");
    config.logger.Log("\n");

    const size_t vpart_table_start = fvm::kVPartTableOffset;
    const size_t vpart_entry_size = sizeof(fvm::vpart_entry_t);
    const size_t vpart_table_size = fvm::kVPartTableLength;
    const size_t vpart_table_end = vpart_table_start + vpart_table_size;
    config.logger.Log("[  Virtual Partition Table  ]\n");
    config.logger.Log("VPartition Entry Start: 0x%016zx\n", vpart_table_start);
    config.logger.Log(" VPartition entry size: %zu\n", vpart_entry_size);
    config.logger.Log(" VPartition table size: %zu\n", vpart_table_size);
    config.logger.Log("  VPartition table end: 0x%016zx\n", vpart_table_end);
    config.logger.Log("\n");

    const size_t slice_table_start = fvm::kAllocTableOffset;
    const size_t slice_entry_size = sizeof(fvm::slice_entry_t);
    const size_t slice_table_size = slice_entry_size * slice_count;
    const size_t slice_table_end = slice_table_start + slice_table_size;
    config.logger.Log("[  Slice Allocation Table  ]\n");
    config.logger.Log("Slice table start: 0x%016zx\n", slice_table_start);
    config.logger.Log(" Slice entry size: %zu\n", slice_entry_size);
    config.logger.Log(" Slice table size: %zu\n", slice_table_size);
    config.logger.Log("  Slice table end: 0x%016zx\n", slice_table_end);
    config.logger.Log("\n");

    const fvm::slice_entry_t* slice_table = reinterpret_cast<const fvm::slice_entry_t*>(
            info.valid_metadata + slice_table_start);
    const fvm::vpart_entry_t* vpart_table = reinterpret_cast<const fvm::vpart_entry_t*>(
            info.valid_metadata + vpart_table_start);

    fbl::Vector<Slice> slices;
    fbl::Array<Partition> partitions;
    bool valid = true;
    if (!LoadPartitions(slice_count, slice_table, vpart_table, &slices, &partitions)) {
        valid = false;
        config.logger.Log("Partitions invalid; displaying info anyway...\n");
    }

    config.logger.Log("[  Partition Info  ]\n");
    for (size_t i = 1; i < FVM_MAX_ENTRIES; i++) {
        const uint32_t slices = vpart_table[i].slices;
        if (slices != 0) {
            char guid_string[GPT_GUID_STRLEN];
            uint8_to_guid_string(guid_string, vpart_table[i].type);
            config.logger.Log("Partition %zu allocated\n", i);
            config.logger.Log("  Has %u slices allocated\n", slices);
            config.logger.Log("  Type: %s\n", gpt_guid_to_type(guid_string));
            config.logger.Log("  Name: %.*s\n", FVM_NAME_LEN, vpart_table[i].name);
        }
    }
    config.logger.Log("\n");

    DumpSlices(config, slices);
    return valid;
}

}  // namespace

int main(int argc, char** argv) {
    Config config = {nullptr, 512, Logger(false)};
    if (!GetOptions(argc, argv, &config)) {
        fprintf(stderr, "%s\n", kUsageMessage);
        return -1;
    }

    if (!ValidateOptions(config)) {
        return -1;
    }

    FvmInfo info;
    if (!LoadFVM(config, &info)) {
        return -1;
    }

    if (!CheckFVM(config, info)) {
        return -1;
    }
    return 0;
}
