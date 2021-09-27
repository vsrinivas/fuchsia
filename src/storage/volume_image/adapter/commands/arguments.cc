// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/hw/gpt.h>

#include <array>
#include <charconv>
#include <iostream>
#include <system_error>
#include <utility>

#include "lib/stdcompat/array.h"
#include "src/storage/fvm/format.h"
#include "src/storage/volume_image/adapter/commands.h"
#include "src/storage/volume_image/options.h"

namespace storage::volume_image {
namespace {

// Returns the first matching argument in |arguments| with |name|.
std::optional<size_t> FindArgumentByName(fbl::Span<std::string_view> arguments,
                                         std::string_view name) {
  for (size_t i = 0; i < arguments.size(); ++i) {
    if (arguments[i] == name) {
      return i;
    }
  }

  return std::nullopt;
}

// Given an arguments |name| look for it in |arguments|. If it exists, get its value.
// Expecting a value, of an argument, and such value not being present is considered a malformed
// argument.
// If presence is required, then |FindArgumentByName| should be called instead.
fpromise::result<std::optional<size_t>, std::string> FindArgumentValueByName(
    fbl::Span<std::string_view> arguments, std::string_view name) {
  auto argument_index = FindArgumentByName(arguments, name);

  if (!argument_index.has_value()) {
    return fpromise::ok(std::nullopt);
  }

  size_t maybe_value_index = argument_index.value() + 1;
  if (maybe_value_index >= arguments.size()) {
    return fpromise::error("No value for argument " + std::string(name));
  }
  auto arg = arguments[maybe_value_index];
  if (arg.substr(0, 2) == "--") {
    return fpromise::error("No value for argument " + std::string(name));
  }

  return fpromise::ok(maybe_value_index);
}

// Parses a size string, to the appropiate unit.
fpromise::result<uint64_t, std::string> ParseSize(std::string_view size_str) {
  // Maybe it has a special size.
  uint64_t result = 0;
  auto [p, ec] = std::from_chars(size_str.begin(), size_str.end(), result);
  if (ec != std::errc{}) {
    return fpromise::error("Failed to parse " + std::string(size_str) + " as size.");
  }

  if (static_cast<size_t>(p - size_str.data()) != size_str.size()) {
    size_t offset = p - size_str.data();
    auto size_unit = size_str.substr(offset);
    switch (size_unit[0]) {
      case 'G':
      case 'g':
        result *= 1024;
        __FALLTHROUGH;
      case 'M':
      case 'm':
        result *= 1024;
        __FALLTHROUGH;
      case 'K':
      case 'k':
        result *= 1024;
        break;

      default:
        return fpromise::error("Failed to parse value" + std::string(size_str) +
                               " as size but unit " + std::string(size_unit) +
                               " is not recognized.");
    }
  }
  return fpromise::ok(result);
}

// If |arguments| contains |argument| and |argument| has a value, then |target| is updated to the
// converted value.
template <typename T>
fpromise::result<void, std::string> GetArgumentValue(fbl::Span<std::string_view> arguments,
                                                     std::string_view argument, T& target) {
  auto index_or = FindArgumentValueByName(arguments, argument);
  if (index_or.is_error()) {
    return index_or.take_error_result();
  }
  auto index = index_or.take_value();
  if (index.has_value()) {
    target = arguments[index.value()];
  }

  return fpromise::ok();
}

// Templated so optionals can be passed as well.
// If |arguments| contains |argument| and |argument|'s value is a valid representation of a size
// value, then |target| is updated to the converted value.
template <typename T>
fpromise::result<void, std::string> GetSizeArgumentValue(fbl::Span<std::string_view> arguments,
                                                         std::string_view argument, T& target) {
  std::optional<std::string> size_str;
  if (auto result = GetArgumentValue(arguments, argument, size_str); result.is_error()) {
    return result.take_error_result();
  }

  if (size_str.has_value()) {
    auto size_or = ParseSize(*size_str);
    if (size_or.is_error()) {
      return size_or.take_error_result();
    }
    target = size_or.value();
  }

  return fpromise::ok();
}

}  // namespace

fpromise::result<std::vector<PartitionParams>, std::string> PartitionParams::FromArguments(
    fbl::Span<std::string_view> arguments, const FvmOptions& options) {
  constexpr std::array<std::string_view, 5> kPartitionArgs = {"--blob", "--data", "--data-unsafe",
                                                              "--system", "--default"};
  std::vector<size_t> partition_args_indexes;
  std::vector<PartitionParams> partitions;

  for (size_t i = 0; i < arguments.size(); ++i) {
    auto it = std::find(kPartitionArgs.begin(), kPartitionArgs.end(), arguments[i]);
    if (it != kPartitionArgs.end()) {
      partition_args_indexes.push_back(i);
    }
  }

  // For each partition arg.
  for (size_t i = 0; i < partition_args_indexes.size(); ++i) {
    size_t argument_index = partition_args_indexes[i];
    size_t next_argument_index =
        (i + 1 == partition_args_indexes.size()) ? arguments.size() : partition_args_indexes[i + 1];
    auto argument_range = arguments.subspan(argument_index, next_argument_index - argument_index);
    auto arg = argument_range[0];

    PartitionParams params;
    params.encrypted = arg == "--data";
    params.format = arg == "--blob" ? PartitionImageFormat::kBlobfs : PartitionImageFormat::kMinfs;
    auto potential_label = arg.substr(2);
    params.label = potential_label == "blob" ? "" : potential_label;

    if (auto result = GetArgumentValue(argument_range, arg, params.source_image_path);
        result.is_error()) {
      return result.take_error_result();
    }

    if (auto result = GetSizeArgumentValue(argument_range, "--minimum-inodes",
                                           params.options.min_inode_count);
        result.is_error()) {
      return result.take_error_result();
    }

    if (auto result = GetSizeArgumentValue(argument_range, "--minimum-data-bytes",
                                           params.options.min_data_bytes);
        result.is_error()) {
      return result.take_error_result();
    }

    if (auto result =
            GetSizeArgumentValue(argument_range, "--maximum-bytes", params.options.max_bytes);
        result.is_error()) {
      return result.take_error_result();
    }
    partitions.push_back(params);
  }

  // One-off empty partition with a label of "data". This will later be reformatted.
  // TODO(fxbug.dev/85165): Have assembly pass in an empty file and remove this flag.
  if (auto index = FindArgumentByName(arguments, "--with-empty-minfs"); index.has_value()) {
    PartitionParams empty_data_partition;
    empty_data_partition.format = PartitionImageFormat::kEmptyPartition;
    empty_data_partition.label = "data";
    empty_data_partition.type_guid = GUID_DATA_VALUE;
    // Doesn't need to be encrypted, it will be found by GUID and label and reformated.
    empty_data_partition.encrypted = false;
    // Need 2 slices - one for zxcrypt and one for minfs.
    empty_data_partition.options.max_bytes = options.slice_size + 1;

    partitions.push_back(empty_data_partition);
  }

  // One-off empty partition with a label of "account". This will later be reformatted.
  // TODO(fxbug.dev/85165): Have assembly pass in an empty file and remove this flag.
  if (auto index = FindArgumentByName(arguments, "--with-empty-account-partition");
      index.has_value()) {
    PartitionParams empty_account_partition;
    empty_account_partition.format = PartitionImageFormat::kEmptyPartition;
    empty_account_partition.label = "account";
    empty_account_partition.type_guid = GUID_DATA_VALUE;
    // Doesn't need to be encrypted, it will be found by GUID and label and reformated.
    empty_account_partition.encrypted = false;
    // Need 2 slices - one for zxcrypt and one for minfs.
    empty_account_partition.options.max_bytes = options.slice_size + 1;

    partitions.push_back(empty_account_partition);
  }

  // One off reserved partition.
  std::optional<uint64_t> reserved_slices;
  if (auto result = GetSizeArgumentValue(arguments, "--reserve-slices", reserved_slices);
      result.is_error()) {
    return result.take_error_result();
  }

  if (reserved_slices.has_value() && *reserved_slices > 0) {
    PartitionParams empty_metadata_partition;
    empty_metadata_partition.format = PartitionImageFormat::kEmptyPartition;
    empty_metadata_partition.label = "internal";
    empty_metadata_partition.type_guid = fvm::kReservedPartitionTypeGuid;
    empty_metadata_partition.encrypted = false;
    empty_metadata_partition.options.max_bytes = reserved_slices.value() * options.slice_size;

    partitions.push_back(empty_metadata_partition);
  }

  return fpromise::ok(partitions);
}

fpromise::result<CreateParams, std::string> CreateParams::FromArguments(
    fbl::Span<std::string_view> arguments) {
  // Create takes an output path, and is of the form:
  // bin output_path create/sparse args
  if (arguments.size() < 3) {
    return fpromise::error("Not enough arguments for 'create' or 'sparse' command.");
  }

  CreateParams params;
  auto command = CommandFromString(arguments[2]);
  switch (command) {
    case Command::kCreate:
      params.format = FvmImageFormat::kBlockImage;
      break;
    case Command::kCreateSparse:
      params.format = FvmImageFormat::kSparseImage;
      break;
    default:
      return fpromise::error("Malformed 'create' command. Found " + std::string(arguments[2]) +
                             " and expected 'create' or 'sparse'.");
  }
  params.output_path = arguments[1];

  if (auto result = GetSizeArgumentValue(arguments, "--offset", params.offset); result.is_error()) {
    return result.take_error_result();
  }
  params.is_output_embedded = params.offset.has_value();

  if (auto result = GetSizeArgumentValue(arguments, "--length", params.length); result.is_error()) {
    return result.take_error_result();
  }
  params.fvm_options.target_volume_size = params.length;

  if (auto result = GetSizeArgumentValue(arguments, "--slice", params.fvm_options.slice_size);
      result.is_error()) {
    return result.take_error_result();
  }

  if (FindArgumentByName(arguments, "--resize-image-file-to-fit")) {
    params.trim_image = true;
  }

  if (auto result =
          GetSizeArgumentValue(arguments, "--max-disk-size", params.fvm_options.max_volume_size);
      result.is_error()) {
    return result.take_error_result();
  }

  std::optional<std::string> compression_type;
  if (auto result = GetArgumentValue(arguments, "--compress", compression_type);
      result.is_error()) {
    return result.take_error_result();
  }

  if (compression_type.has_value()) {
    if (compression_type.value() != "lz4") {
      return fpromise::error("Unsupported compression type'" + compression_type.value() +
                             "'. Currently only 'lz4' compression type is supported.");
    }
    params.fvm_options.compression.schema = CompressionSchema::kLz4;
  }

  auto partition_params_or = PartitionParams::FromArguments(arguments, params.fvm_options);
  if (partition_params_or.is_error()) {
    return partition_params_or.take_error_result();
  }
  params.partitions = partition_params_or.take_value();

  // We cant generate an image with encrypted contents.
  if (params.format == FvmImageFormat::kBlockImage) {
    for (auto& partition : params.partitions) {
      partition.encrypted = false;
    }
  }

  return fpromise::ok(params);
}

fpromise::result<PaveParams, std::string> PaveParams::FromArguments(
    fbl::Span<std::string_view> arguments) {
  PaveParams params;
  if (arguments.size() < 3) {
    return fpromise::error("Not enough arguments for 'pave' command.");
  }
  auto command = CommandFromString(arguments[2]);
  if (command != Command::kPave) {
    return fpromise::error("Pave must be invoked with comman 'pave'.");
  }

  params.output_path = arguments[1];

  if (auto result = GetSizeArgumentValue(arguments, "--offset", params.offset); result.is_error()) {
    return result.take_error_result();
  }
  params.is_output_embedded = params.offset.has_value();

  if (auto result = GetSizeArgumentValue(arguments, "--length", params.length); result.is_error()) {
    return result.take_error_result();
  }
  params.fvm_options.target_volume_size = params.length;

  if (auto result =
          GetSizeArgumentValue(arguments, "--max-disk-size", params.fvm_options.max_volume_size);
      result.is_error()) {
    return result.take_error_result();
  }

  if (auto result = GetArgumentValue(arguments, "--sparse", params.input_path); result.is_error()) {
    return result.take_error_result();
  }

  std::optional<std::string> target_type;
  if (auto result = GetArgumentValue(arguments, "--disk-type", target_type); result.is_error()) {
    return result.take_error_result();
  }

  if (auto result = GetSizeArgumentValue(arguments, "--max-bad-blocks", params.max_bad_blocks);
      result.is_error()) {
    return result.take_error_result();
  }

  // Default is |File|.
  if (!target_type.has_value() || target_type.value() == "file") {
    params.type = TargetType::kFile;
  } else if (target_type.value() == "mtd") {
    params.type = TargetType::kMtd;
  } else if (target_type.value() == "block_device") {
    params.type = TargetType::kBlockDevice;
  }

  return fpromise::ok(params);
}

fpromise::result<ExtendParams, std::string> ExtendParams::FromArguments(
    fbl::Span<std::string_view> arguments) {
  ExtendParams params;
  if (arguments.size() < 3) {
    return fpromise::error("Not enough arguments for 'extend' command.");
  }
  auto command = CommandFromString(arguments[2]);
  if (command != Command::kExtend) {
    return fpromise::error("Extend must be invoked with command 'extend'.");
  }

  params.image_path = arguments[1];

  if (auto result = GetSizeArgumentValue(arguments, "--length", params.length); result.is_error()) {
    return result.take_error_result();
  }

  params.should_use_max_partition_size =
      FindArgumentByName(arguments, "--length-is-lowerbound").has_value();
  params.should_trim = FindArgumentByName(arguments, "--resize-image-file-to-fit").has_value();

  return fpromise::ok(params);
}

fpromise::result<SizeParams, std::string> SizeParams::FromArguments(
    fbl::Span<std::string_view> arguments) {
  SizeParams params;
  if (arguments.size() < 3) {
    return fpromise::error("Not enough arguments for 'size' command.");
  }
  auto command = CommandFromString(arguments[2]);
  if (command != Command::kSize) {
    return fpromise::error("Size must be invoked with command 'size'.");
  }

  params.image_path = arguments[1];

  if (auto result = GetSizeArgumentValue(arguments, "--disk", params.length); result.is_error()) {
    return result.take_error_result();
  }

  return fpromise::ok(params);
}

Command CommandFromString(std::string_view command_str) {
  static constexpr auto kCommandStringToCommand = cpp20::to_array({
      std::make_pair("create", Command::kCreate),
      std::make_pair("sparse", Command::kCreateSparse),
      std::make_pair("pave", Command::kPave),
      std::make_pair("extend", Command::kExtend),
      std::make_pair("size", Command::kSize),
  });
  for (const auto& [str, command] : kCommandStringToCommand) {
    if (str == command_str) {
      return command;
    }
  }
  return Command::kUnsupported;
}

}  // namespace storage::volume_image
