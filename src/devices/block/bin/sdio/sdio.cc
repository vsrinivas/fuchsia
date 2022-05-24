// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdio.h"

#include <lib/zx/clock.h>

#include <fbl/algorithm.h>

namespace sdio {

using SdioClient = fidl::WireSyncClient<fuchsia_hardware_sdio::Device>;
using namespace fuchsia_hardware_sdio;
using namespace fuchsia_hardware_sdio::wire;

constexpr char kUsageMessage[] = R"""(Usage: sdio <device> <command> [options]

    --help - Show this message
    --version - Show the version of this tool
    info - Display information about the host controller and the card
    read-byte <address> - Read one byte from the SDIO function
    write-byte <address> <byte> - Write one byte to the SDIO function
    read-stress <address> <size> <loops> [--fifo] [--dma] - Read a number of blocks from the SDIO
                                                            function and measure the throughput

    Example:
    sdio /dev/class/sdio/001 read-stress 0x01234 256 100 dma
)""";

constexpr char kVersion[] = "1";

void PrintUsage() { printf("%s", kUsageMessage); }

void PrintVersion() { printf("%s\n", kVersion); }

template <typename T>
bool ParseNumericalArg(const char* const arg, T* const out) {
  char* endptr;
  unsigned long value = strtoul(arg, &endptr, 0);
  if (*endptr != '\0') {
    fprintf(stderr, "Failed to parse value: %s\n", arg);
    return false;
  }

  if constexpr (sizeof(T) < sizeof(value)) {
    if (value > ((1ul << (sizeof(T) * 8)) - 1)) {
      fprintf(stderr, "Value out of range: %s\n", arg);
      return false;
    }
  }

  *out = static_cast<T>(value);
  return true;
}

std::string GetTxnStats(const zx::duration duration, const uint64_t bytes) {
  constexpr size_t kMaxStringSize = 32;

  constexpr double kKilobyte = 1000.0;
  constexpr double kMegabyte = kKilobyte * 1000.0;
  constexpr double kGigabyte = kMegabyte * 1000.0;

  char duration_str[kMaxStringSize];
  const double duration_nsec = static_cast<double>(duration.to_nsecs());
  if (duration >= zx::sec(1)) {
    snprintf(duration_str, kMaxStringSize, "%.3f s", duration_nsec / zx::sec(1).to_nsecs());
  } else if (duration >= zx::msec(1)) {
    snprintf(duration_str, kMaxStringSize, "%.3f ms", duration_nsec / zx::msec(1).to_nsecs());
  } else if (duration >= zx::usec(1)) {
    snprintf(duration_str, kMaxStringSize, "%.3f us", duration_nsec / zx::usec(1).to_nsecs());
  } else {
    snprintf(duration_str, kMaxStringSize, "%ld ns", duration.to_nsecs());
  }

  if (duration.to_nsecs() == 0) {
    return std::string(duration_str);
  }

  char bytes_second_str[kMaxStringSize];
  const double bytes_second = static_cast<double>(bytes) / (duration_nsec / zx::sec(1).to_nsecs());
  if (bytes_second >= kGigabyte) {
    snprintf(bytes_second_str, kMaxStringSize, " (%.3f GB/s)", bytes_second / kGigabyte);
  } else if (bytes_second >= kMegabyte) {
    snprintf(bytes_second_str, kMaxStringSize, " (%.3f MB/s)", bytes_second / kMegabyte);
  } else if (bytes_second >= kKilobyte) {
    snprintf(bytes_second_str, kMaxStringSize, " (%.3f kB/s)", bytes_second / kKilobyte);
  } else {
    snprintf(bytes_second_str, kMaxStringSize, " (%.3f B/s)", bytes_second);
  }

  return std::string(duration_str) + std::string(bytes_second_str);
}

int Info(SdioClient client) {
  struct {
    SdioDeviceCapabilities capability;
    const char* string;
  } constexpr kCapabilityStrings[] = {
      {SdioDeviceCapabilities::kMultiBlock, "MULTI_BLOCK"},
      {SdioDeviceCapabilities::kSrw, "SRW"},
      {SdioDeviceCapabilities::kDirectCommand, "DIRECT_COMMAND"},
      {SdioDeviceCapabilities::kSuspendResume, "SUSPEND_RESUME"},
      {SdioDeviceCapabilities::kLowSpeed, "LOW_SPEED"},
      {SdioDeviceCapabilities::kHighSpeed, "HIGH_SPEED"},
      {SdioDeviceCapabilities::kHighPower, "HIGH_POWER"},
      {SdioDeviceCapabilities::kFourBitBus, "FOUR_BIT_BUS"},
      {SdioDeviceCapabilities::kHsSdr12, "HS_SDR12"},
      {SdioDeviceCapabilities::kHsSdr25, "HS_SDR25"},
      {SdioDeviceCapabilities::kUhsSdr50, "UHS_SDR50"},
      {SdioDeviceCapabilities::kUhsSdr104, "UHS_SDR104"},
      {SdioDeviceCapabilities::kUhsDdr50, "UHS_DDR50"},
      {SdioDeviceCapabilities::kTypeA, "TYPE_A"},
      {SdioDeviceCapabilities::kTypeB, "TYPE_B"},
      {SdioDeviceCapabilities::kTypeC, "TYPE_C"},
      {SdioDeviceCapabilities::kTypeD, "TYPE_D"},
  };

  auto result = client->GetDevHwInfo();
  if (!result.ok()) {
    fprintf(stderr, "FIDL call GetDevHwInfo failed: %d\n", result.status());
    return 1;
  }
  if (result.Unwrap_NEW()->is_error()) {
    fprintf(stderr, "GetDevHwInfo failed: %d\n", result.Unwrap_NEW()->error_value());
    return 1;
  }

  const SdioHwInfo& info = result.Unwrap_NEW()->value()->hw_info;
  const SdioDeviceHwInfo& dev_info = info.dev_hw_info;
  printf("Host:\n    Max transfer size: %u\n", info.host_max_transfer_size);
  printf("Card:\n");
  printf(
      "    SDIO version: %u\n"
      "    CCCR version: %u\n"
      "    Capabilities: 0x%08x\n",
      dev_info.sdio_vsn, dev_info.cccr_vsn, dev_info.caps);

  for (size_t i = 0; i < std::size(kCapabilityStrings); i++) {
    if (dev_info.caps & static_cast<uint32_t>(kCapabilityStrings[i].capability)) {
      printf("        %s\n", kCapabilityStrings[i].string);
    }
  }

  for (uint32_t i = 0; i < dev_info.num_funcs; i++) {
    printf("    Function %u:\n", i);
    const SdioFuncHwInfo& func_info = info.funcs_hw_info[i];
    printf(
        "        Manufacturer ID:    0x%04x\n"
        "        Product ID:         0x%04x\n"
        "        Max block size:     %u\n",
        func_info.manufacturer_id, func_info.product_id, func_info.max_blk_size);

    if (i == 0) {
      printf("        Max transfer speed: ");
      if (func_info.max_tran_speed > 1000) {
        printf("%.1f Mb/s\n", static_cast<double>(func_info.max_tran_speed) / 1000.0);
      } else {
        printf("%u kb/s\n", func_info.max_tran_speed);
      }
    } else {
      printf("        Interface code:     0x%02x\n", func_info.fn_intf_code);
    }
  }

  return 0;
}

int ReadByte(SdioClient client, uint32_t address, int argc, const char** argv) {
  auto result = client->DoRwByte(false, address, 0);
  if (!result.ok()) {
    fprintf(stderr, "FIDL call DoRwByte failed: %d\n", result.status());
    return 1;
  }
  if (result.Unwrap_NEW()->is_error()) {
    fprintf(stderr, "DoRwByte failed: %d\n", result.Unwrap_NEW()->error_value());
    return 1;
  }

  printf("0x%02x\n", result.Unwrap_NEW()->value()->read_byte);
  return 0;
}

int WriteByte(SdioClient client, uint32_t address, int argc, const char** argv) {
  if (argc < 1) {
    fprintf(stderr, "Expected <byte> argument\n");
    PrintUsage();
    return 1;
  }

  uint8_t write_value = 0;
  if (!ParseNumericalArg(argv[0], &write_value)) {
    return 1;
  }

  auto result = client->DoRwByte(true, address, write_value);
  if (!result.ok()) {
    fprintf(stderr, "FIDL call DoRwByte failed: %d\n", result.status());
    return 1;
  }
  if (result.Unwrap_NEW()->is_error()) {
    fprintf(stderr, "DoRwByte failed: %d\n", result.Unwrap_NEW()->error_value());
    return 1;
  }

  return 0;
}

int ReadStress(SdioClient client, uint32_t address, int argc, const char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Expected <size> and <loops> arguments\n");
    PrintUsage();
    return 1;
  }

  uint32_t size = 0;
  if (!ParseNumericalArg(argv[0], &size)) {
    return 1;
  }

  unsigned long loops = 0;
  if (!ParseNumericalArg(argv[1], &loops)) {
    return 1;
  }

  bool incr = true;
  bool use_dma = false;

  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--fifo") == 0) {
      incr = false;
    } else if (strcmp(argv[i], "--dma") == 0) {
      use_dma = true;
    } else {
      fprintf(stderr, "Unexpected option: %s\n", argv[i]);
      PrintUsage();
      return 1;
    }
  }

  std::unique_ptr<uint8_t[]> buffer;

  zx::vmo dma_vmo;
  if (use_dma) {
    zx_status_t status = zx::vmo::create(size, 0, &dma_vmo);
    if (status != ZX_OK) {
      fprintf(stderr, "Failed to create VMO: %d\n", status);
      return 1;
    }
  } else {
    buffer = std::unique_ptr<uint8_t[]>(new uint8_t[size]);
  }

  const zx::time start = zx::clock::get_monotonic();

  for (unsigned long i = 0; i < loops; i++) {
    SdioRwTxn txn = {};
    txn.addr = address;
    txn.data_size = size;
    txn.incr = incr;
    txn.write = false;
    txn.use_dma = use_dma;
    txn.buf_offset = 0;

    if (use_dma) {
      txn.dma_vmo = std::move(dma_vmo);
    } else {
      txn.virt = fidl::VectorView<uint8_t>::FromExternal(buffer.get(), size);
    }

    auto result = client->DoRwTxn(std::move(txn));
    if (!result.ok()) {
      fprintf(stderr, "FIDL call DoRwTxn failed: %d\n", result.status());
      return 1;
    }
    if (result.Unwrap_NEW()->is_error()) {
      fprintf(stderr, "DoRwTxn failed: %d\n", result.Unwrap_NEW()->error_value());
      return 1;
    }

    dma_vmo = std::move(result.Unwrap_NEW()->value()->txn.dma_vmo);
  }

  const zx::duration elapsed = zx::clock::get_monotonic() - start;
  const std::string txn_stats = GetTxnStats(elapsed, size * loops);
  printf("Read %lu chunks of %u bytes in %s\n", loops, size, txn_stats.c_str());
  return 0;
}

int RunSdioTool(SdioClient client, int argc, const char** argv) {
  if (argc < 1) {
    fprintf(stderr, "Expected <command> argument\n");
    PrintUsage();
    return 1;
  }

  const char* const command = argv[0];
  if (strcmp(command, "info") == 0) {
    return Info(std::move(client));
  }

  if (argc < 2) {
    fprintf(stderr, "Expected <address> argument\n");
    PrintUsage();
    return 1;
  }

  const char* const address_str = argv[1];

  argc -= 2;
  argv += 2;

  uint32_t address = 0;
  if (!ParseNumericalArg(address_str, &address)) {
    return 1;
  }

  constexpr uint32_t kMaxSdioAddress = (1 << 17) - 1;

  if (address > kMaxSdioAddress) {
    fprintf(stderr, "Address must be less than 0x%x: %s\n", kMaxSdioAddress, address_str);
    return 1;
  }

  if (strcmp(command, "read-byte") == 0) {
    return ReadByte(std::move(client), address, argc, argv);
  } else if (strcmp(command, "write-byte") == 0) {
    return WriteByte(std::move(client), address, argc, argv);
  } else if (strcmp(command, "read-stress") == 0) {
    return ReadStress(std::move(client), address, argc, argv);
  } else {
    fprintf(stderr, "Unexpected command: %s\n", command);
    PrintUsage();
    return 1;
  }
}

}  // namespace sdio
