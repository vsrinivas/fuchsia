// Copyright (c) 2019 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/firmware.h"

#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <cctype>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"

namespace wlan {
namespace brcmfmac {
namespace {

struct FirmwareMapping {
  CommonCoreId chip_id;
  uint32_t chip_rev_mask;
  const char* firmware_filename;
  const char* nvram_filename;
};

constexpr char kDefaultFirmwarePath[] = "brcmfmac/";

constexpr FirmwareMapping kSdioFirmwareMappings[] = {
    {CommonCoreId::kBrcm43143, 0xFFFFFFFF, "brcmfmac43143-sdio.bin", "brcmfmac43143-sdio.txt"},
    {CommonCoreId::kBrcm43241, 0x0000001F, "brcmfmac43241b0-sdio.bin", "brcmfmac43241b0-sdio.txt"},
    {CommonCoreId::kBrcm43241, 0x00000020, "brcmfmac43241b4-sdio.bin", "brcmfmac43241b4-sdio.txt"},
    {CommonCoreId::kBrcm43241, 0xFFFFFFC0, "brcmfmac43241b5-sdio.bin", "brcmfmac43241b5-sdio.txt"},
    {CommonCoreId::kBrcm4329, 0xFFFFFFFF, "brcmfmac4329-sdio.bin", "brcmfmac4329-sdio.txt"},
    {CommonCoreId::kBrcm4330, 0xFFFFFFFF, "brcmfmac4330-sdio.bin", "brcmfmac4330-sdio.txt"},
    {CommonCoreId::kBrcm4334, 0xFFFFFFFF, "brcmfmac4334-sdio.bin", "brcmfmac4334-sdio.txt"},
    {CommonCoreId::kBrcm43340, 0xFFFFFFFF, "brcmfmac43340-sdio.bin", "brcmfmac43340-sdio.txt"},
    {CommonCoreId::kBrcm43341, 0xFFFFFFFF, "brcmfmac43340-sdio.bin", "brcmfmac43340-sdio.txt"},
    {CommonCoreId::kBrcm4335, 0xFFFFFFFF, "brcmfmac4335-sdio.bin", "brcmfmac4335-sdio.txt"},
    {CommonCoreId::kBrcm43362, 0xFFFFFFFE, "brcmfmac43362-sdio.bin", "brcmfmac43362-sdio.txt"},
    {CommonCoreId::kBrcm4339, 0xFFFFFFFF, "brcmfmac4339-sdio.bin", "brcmfmac4339-sdio.txt"},
    {CommonCoreId::kBrcm43430, 0x00000001, "brcmfmac43430a0-sdio.bin", "brcmfmac43430a0-sdio.txt"},
    {CommonCoreId::kBrcm43430, 0xFFFFFFFE, "brcmfmac43430-sdio.bin", "brcmfmac43430-sdio.txt"},
    {CommonCoreId::kBrcm4345, 0xFFFFFFC0, "brcmfmac43455-sdio.bin", "brcmfmac43455-sdio.txt"},
    {CommonCoreId::kBrcm4354, 0xFFFFFFFF, "brcmfmac4354-sdio.bin", "brcmfmac4354-sdio.txt"},
    {CommonCoreId::kBrcm4356, 0xFFFFFFFF, "brcmfmac4356-sdio.bin", "brcmfmac4356-sdio.txt"},
    {CommonCoreId::kBrcm4359, 0xFFFFFFFF, "brcmfmac4359-sdio.bin", "brcmfmac4359-sdio.txt"},
    {CommonCoreId::kCypress4373, 0xFFFFFFFF, "brcmfmac4373-sdio.bin", "brcmfmac4373-sdio.txt"},
};

constexpr FirmwareMapping kPcieFirmwareMappings[] = {
    {CommonCoreId::kBrcm4356, 0xFFFFFFFF, "brcmfmac4356-pcie.bin", "brcmfmac4356-pcie.txt"},
};

const FirmwareMapping* GetFirmwareMapping(brcmf_bus_type bus_type, CommonCoreId chip_id,
                                          uint32_t chip_rev) {
  switch (bus_type) {
    case brcmf_bus_type::BRCMF_BUS_TYPE_SDIO: {
      for (const auto& mapping : kSdioFirmwareMappings) {
        if (chip_id == mapping.chip_id && ((1 << chip_rev) & mapping.chip_rev_mask)) {
          return &mapping;
        }
      }
      break;
    }
    case brcmf_bus_type::BRCMF_BUS_TYPE_PCIE: {
      for (const auto& mapping : kPcieFirmwareMappings) {
        if (chip_id == mapping.chip_id && ((1 << chip_rev) & mapping.chip_rev_mask)) {
          return &mapping;
        }
      }
      break;
    }
    default:
      break;
  }

  BRCMF_ERR("No firmware/NVRAM mapping found for bus_type=%d, chip_id=0x%x, chip_rev=%d",
            static_cast<int>(bus_type), static_cast<int>(chip_id), static_cast<int>(chip_rev));
  return nullptr;
}

zx_status_t LoadBinaryFromFile(Device* device, std::string_view filename, std::string* binary_out) {
  zx_status_t status = ZX_OK;
  zx_handle_t vmo_handle = ZX_HANDLE_INVALID;
  size_t vmo_size = 0;
  const auto filepath = std::string(kDefaultFirmwarePath).append(filename);
  if ((status = device->LoadFirmware(filepath.c_str(), &vmo_handle, &vmo_size)) != ZX_OK) {
    BRCMF_ERR("Failed to load filepath %s: %s", filepath.c_str(), zx_status_get_string(status));
    return status;
  }

  std::string binary_data(vmo_size, '\0');
  if ((status = zx_vmo_read(vmo_handle, binary_data.data(), 0, binary_data.size())) != ZX_OK) {
    BRCMF_ERR("Failed to read filepath %s: %s", filepath.c_str(), zx_status_get_string(status));
    return status;
  }

  *binary_out = std::move(binary_data);
  return ZX_OK;
}

}  // namespace

zx_status_t GetFirmwareBinary(Device* device, brcmf_bus_type bus_type, CommonCoreId chipid,
                              uint32_t chiprev, std::string* binary_out) {
  const FirmwareMapping* firmware_mapping = GetFirmwareMapping(bus_type, chipid, chiprev);
  if (firmware_mapping == nullptr) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return LoadBinaryFromFile(device, firmware_mapping->firmware_filename, binary_out);
}

zx_status_t GetClmBinary(Device* device, brcmf_bus_type bus_type, CommonCoreId chip_id,
                         uint32_t chip_rev, std::string* binary_out) {
  const FirmwareMapping* firmware_mapping = GetFirmwareMapping(bus_type, chip_id, chip_rev);
  if (firmware_mapping == nullptr) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  const std::string_view firmware_name(firmware_mapping->firmware_filename);
  const std::string clm_name =
      std::string(firmware_name.substr(0, firmware_name.find_last_of('.'))).append(".clm_blob");

  return LoadBinaryFromFile(device, clm_name, binary_out);
}

// Get the NVRAM binary for the given bus and chip.
zx_status_t GetNvramBinary(Device* device, brcmf_bus_type bus_type, CommonCoreId chip_id,
                           uint32_t chip_rev, std::string* binary_out) {
  zx_status_t status = ZX_OK;

  const FirmwareMapping* firmware_mapping = GetFirmwareMapping(bus_type, chip_id, chip_rev);
  if (firmware_mapping == nullptr) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::string binary_data;
  if ((status = LoadBinaryFromFile(device, firmware_mapping->nvram_filename, &binary_data)) !=
      ZX_OK) {
    return status;
  }

  if ((status = ParseNvramBinary(binary_data, binary_out)) != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

// Parse an NVRAM image from file, into a format suitable for uploading to the device.
zx_status_t ParseNvramBinary(std::string_view nvram, std::string* parsed_nvram_out) {
  auto read_iter = nvram.cbegin();
  std::string parsed_nvram;
  // The initial parsing pass only removes characters, so the input size is a good starting point.
  parsed_nvram.reserve(nvram.size());
  int line_index = 1;
  bool boardrev_found = false;

  // Skip whitespace.
  const auto skip_past_blank = [&]() {
    // Note that std::isspace() will munch '\r' as well, for DOS-style newlines.
    while (read_iter != nvram.cend() && std::isspace(*read_iter) && *read_iter != '\n') {
      ++read_iter;
    }
  };

  // Skip to the next line.
  const auto skip_past_newline = [&]() {
    while (true) {
      if (read_iter == nvram.cend()) {
        return;
      }
      const char read_char = *read_iter;
      ++read_iter;
      if (read_char == '\n') {
        ++line_index;
        return;
      }
    }
  };

  while (true) {
    // Skip leading whitespace.
    skip_past_blank();
    if (read_iter == nvram.cend()) {
      break;
    }
    if (*read_iter == '\n') {
      // This was a blank line.
      ++line_index;
      ++read_iter;
      continue;
    }

    // This is a comment.
    if (*read_iter == '#') {
      skip_past_newline();
      continue;
    }

    // This is a key/value pair.  Write it to the output.
    // Keys are named with printable characters (but not spaces), except for '#' which is a comment.
    const auto key_begin = read_iter;
    while (read_iter != nvram.cend() && std::isgraph(*read_iter) && *read_iter != '#' &&
           *read_iter != '=') {
      ++read_iter;
    }
    const std::string_view key(&*key_begin, read_iter - key_begin);
    if (read_iter == key_begin) {
      BRCMF_ERR("Invalid NVRAM key \"%*s\" at line %d", static_cast<int>(key.size()), key.data(),
                line_index);
      return ZX_ERR_INVALID_ARGS;
    }

    // Find the "=" separator for the value, possibly surrounded by blankspace.
    skip_past_blank();
    if (read_iter == nvram.cend() || *read_iter != '=') {
      BRCMF_ERR("Missing NVRAM value for key \"%*s\" at line %d", static_cast<int>(key.size()),
                key.data(), line_index);
      return ZX_ERR_INVALID_ARGS;
    }
    ++read_iter;
    skip_past_blank();
    if (read_iter == nvram.cend()) {
      BRCMF_ERR("Missing NVRAM value for key \"%*s\" at line %d", static_cast<int>(key.size()),
                key.data(), line_index);
      return ZX_ERR_INVALID_ARGS;
    }

    // Values can be printable characters, including spaces, except for '#' which is a comment.
    const auto value_begin = read_iter;
    while (read_iter != nvram.cend() && std::isprint(*read_iter) && *read_iter != '#') {
      ++read_iter;
    }
    // Trim trailing whitespace.
    auto value_end = read_iter;
    while (value_end > value_begin && std::isspace(*(value_end - 1))) {
      --value_end;
    }
    const std::string_view value(&*value_begin, value_end - value_begin);

    // The rest of the line is either whitespace to a newline, or a comment.
    skip_past_newline();
    if (*(read_iter - 1) != '\n') {
      BRCMF_ERR("Missing NVRAM newline after value for key \"%*s\" at line %d",
                static_cast<int>(key.size()), key.data(), line_index);
      return ZX_ERR_INVALID_ARGS;
    }

    // Check for special key values.
    if (key.compare("RAW1") == 0) {
      // Ignore RAW1 lines.
      continue;
    } else if (key.compare(0, 7, "devpath") == 0 || key.compare(0, 5, "pcie/") == 0) {
      // These features are not supported, yet.
      BRCMF_ERR("Unsupported NVRAM key \"%*s\" at line %d", static_cast<int>(key.size()),
                key.data(), line_index);
      continue;
    } else if (key.compare("boardrev") == 0) {
      boardrev_found = true;
    }

    // Write to the output.
    parsed_nvram.append(key);
    parsed_nvram.append(1, '=');
    parsed_nvram.append(value);
    parsed_nvram.append(1, '\0');
  }

  // Append the footer.  The binary has default entries appended, if applicable; then it is padded
  // with an extra '\0', then padded out to 4-byte alignment, then appended with a 4-byte length
  // token.
  const size_t parsed_size = parsed_nvram.size();
  size_t default_values_size = 0;
  const char kDefaultBoardrev[] = "boardrev=0xff";
  if (!boardrev_found) {
    default_values_size += sizeof(kDefaultBoardrev);  // sizeof() includes the trailing '\0'.
  }
  const size_t padding_size = 4 - ((parsed_size + default_values_size + 1) % 4);
  uint32_t token = 0;
  constexpr size_t kTokenSize = sizeof(token);
  parsed_nvram.reserve(parsed_size + default_values_size + 1 + padding_size + kTokenSize);

  // Append the default entries.
  if (!boardrev_found) {
    parsed_nvram.append(kDefaultBoardrev, sizeof(kDefaultBoardrev));  // Include the trailing '\0'.
  }

  // Pad with an extra '\0', then out to 4-byte alignment.
  parsed_nvram.append(1 + padding_size, '\0');

  // Append the length token.
  token = parsed_nvram.size() / 4;
  token = (~token << 16) | (token & 0x0000FFFF);
  parsed_nvram.append(reinterpret_cast<const char*>(&token), sizeof(token));
  parsed_nvram.shrink_to_fit();

  *parsed_nvram_out = std::move(parsed_nvram);
  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan
