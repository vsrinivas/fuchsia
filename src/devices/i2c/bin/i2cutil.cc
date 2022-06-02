// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <zircon/status.h>

#include <filesystem>

#include <fbl/unique_fd.h>

static void usage(char* prog) {
  printf("Usage:\n");
  printf(" (DATA and ADDRESS are a list of space separated bytes BYTE_0 BYTE_1...BYTE_N)\n");
  printf(" %s w[rite]    DEVICE DATA...                                          Write bytes\n",
         prog);
  printf(" %s r[ead]     DEVICE ADDRESS                                          Reads one byte\n",
         prog);
  printf(" %s t[ransact] DEVICE [w|r] [DATA...|LENGTH] [w|r] [DATA...|LENGTH]... Transaction\n",
         prog);
  printf(" %s p[ing]                                                             Ping devices\n",
         prog);
}

template <typename T>
static zx_status_t convert_args(char** argv, size_t length, T* buffer) {
  for (size_t i = 0; i < length; i++) {
    char* end = nullptr;
    unsigned long value = strtoul(argv[i], &end, 0);
    if (value > 0xFF || *end != '\0') {
      return ZX_ERR_INVALID_ARGS;
    }
    buffer[i] = static_cast<T>(value);
  }
  return ZX_OK;
}

static zx_status_t write_bytes(fidl::WireSyncClient<fuchsia_hardware_i2c::Device> client,
                               cpp20::span<uint8_t> write_buffer) {
  auto write_data =
      fidl::VectorView<uint8_t>::FromExternal(write_buffer.data(), write_buffer.size());

  fidl::Arena arena;
  auto transactions = fidl::VectorView<fuchsia_hardware_i2c::wire::Transaction>(arena, 1);
  transactions[0] =
      fuchsia_hardware_i2c::wire::Transaction::Builder(arena)
          .data_transfer(fuchsia_hardware_i2c::wire::DataTransfer::WithWriteData(arena, write_data))
          .Build();

  auto read = client->Transfer(transactions);
  auto status = read.status();
  if (status == ZX_OK && read->is_error()) {
    status = ZX_ERR_INTERNAL;
  }
  return status;
}

static zx::status<uint8_t> read_byte(fidl::WireSyncClient<fuchsia_hardware_i2c::Device> client,
                                     cpp20::span<uint8_t> address) {
  auto write_data = fidl::VectorView<uint8_t>::FromExternal(address.data(), address.size());

  fidl::Arena arena;
  auto transactions = fidl::VectorView<fuchsia_hardware_i2c::wire::Transaction>(arena, 2);
  transactions[0] =
      fuchsia_hardware_i2c::wire::Transaction::Builder(arena)
          .data_transfer(fuchsia_hardware_i2c::wire::DataTransfer::WithWriteData(arena, write_data))
          .Build();
  transactions[1] = fuchsia_hardware_i2c::wire::Transaction::Builder(arena)
                        .data_transfer(fuchsia_hardware_i2c::wire::DataTransfer::WithReadSize(1))
                        .Build();

  auto read = client->Transfer(transactions);
  auto status = read.status();
  if (status != ZX_OK) {
    return zx::error(read.status());
  }
  if (read->is_error()) {
    return zx::error(read->error_value());
  }
  return zx::ok(read->value()->read_data[0].data()[0]);
}

static zx_status_t transact(fidl::WireSyncClient<fuchsia_hardware_i2c::Device> client, int argc,
                            char** argv) {
  size_t n_elements = argc - 3;
  size_t n_segments = 0;
  size_t n_writes = 0;
  // We know n_segments and total data will be smaller than n_elements, so we use it as max.
  auto segment_start = std::make_unique<size_t[]>(n_elements);
  auto writes_start = std::make_unique<size_t[]>(n_elements);
  auto write_buffer = std::make_unique<uint8_t[]>(n_elements);

  // Find n_segments, segment starts and writes starts.
  for (size_t i = 0; i < n_elements; ++i) {
    if (argv[3 + i][0] == 'r') {
      segment_start[n_segments++] = i + 1;
    } else if (argv[3 + i][0] == 'w') {
      segment_start[n_segments++] = i + 1;
      writes_start[n_writes++] = i + 1;
    }
  }

  // Must have at least one segment and start with a w or r.
  if (n_segments == 0 || (argv[3][0] != 'r' && argv[3][0] != 'w')) {
    usage(argv[0]);
    return -1;
  }
  if (n_segments > fuchsia_hardware_i2c::wire::kMaxCountTransactions) {
    printf("No more than %u segments allowed\n", fuchsia_hardware_i2c::wire::kMaxCountTransactions);
    return -1;
  }

  // For the last segment we pretend that data starts after a pretend w/r, this makes
  // calculations below consistent for the last actual segment without a segment to follow.
  segment_start[n_segments] = n_elements + 1;

  auto write_data = std::make_unique<fidl::VectorView<uint8_t>[]>(n_writes);
  auto read_lengths = std::make_unique<uint32_t[]>(n_segments - n_writes);
  auto is_write = std::make_unique<bool[]>(n_segments);

  fidl::Arena arena;
  auto transactions = fidl::VectorView<fuchsia_hardware_i2c::wire::Transaction>(arena, n_segments);

  size_t element_cnt = 0;
  size_t segment_cnt = 0;
  size_t read_cnt = 0;
  size_t write_cnt = 0;
  uint8_t* write_buffer_pos = write_buffer.get();
  while (element_cnt < n_elements) {
    if (argv[3 + element_cnt][0] == 'w') {
      is_write[segment_cnt] = true;
      element_cnt++;
    } else if (argv[3 + element_cnt][0] == 'r') {
      is_write[segment_cnt] = false;
      element_cnt++;
    } else {
      if (is_write[segment_cnt]) {
        auto write_len = segment_start[segment_cnt + 1] - segment_start[segment_cnt] - 1;
        auto status = convert_args(&argv[3 + element_cnt], write_len, write_buffer_pos);
        if (status != ZX_OK) {
          usage(argv[0]);
          return status;
        }
        write_data[write_cnt] =
            fidl::VectorView<uint8_t>::FromExternal(write_buffer_pos, write_len);
        transactions[segment_cnt] =
            fuchsia_hardware_i2c::wire::Transaction::Builder(arena)
                .data_transfer(fuchsia_hardware_i2c::wire::DataTransfer::WithWriteData(
                    arena, write_data[write_cnt]))
                .Build();
        write_buffer_pos += write_len;
        write_cnt++;
        element_cnt += write_len;
      } else {
        auto status = convert_args(&argv[3 + element_cnt], 1, &read_lengths[read_cnt]);
        if (status != ZX_OK) {
          usage(argv[0]);
          return status;
        }
        transactions[segment_cnt] =
            fuchsia_hardware_i2c::wire::Transaction::Builder(arena)
                .data_transfer(
                    fuchsia_hardware_i2c::wire::DataTransfer::WithReadSize(read_lengths[read_cnt]))
                .Build();
        read_cnt++;
        element_cnt++;
      }
      segment_cnt++;
    }
  }
  if (write_cnt != n_writes || read_cnt + write_cnt != segment_cnt) {
    usage(argv[0]);
    return ZX_ERR_INVALID_ARGS;
  }

  if (n_writes != 0) {
    printf("Writes:");
    for (size_t i = 0; i < n_writes; ++i) {
      printf(" ");
      for (size_t j = 0; j < write_data[i].count(); ++j) {
        printf("0x%02X ", write_data[i].data()[j]);
      }
    }
    printf("\n");
  }
  auto read = client->Transfer(transactions);
  auto status = read.status();
  if (status == ZX_OK) {
    if (read->is_error()) {
      return ZX_ERR_INTERNAL;
    } else {
      auto& read_data = read->value()->read_data;
      if (read_data.count() != 0) {
        printf("Reads:");
        for (auto& i : read_data) {
          printf(" ");
          for (size_t j = 0; j < i.count(); ++j) {
            printf("0x%02X ", i.data()[j]);
          }
        }
        printf("\n");
      }
    }
  }
  return status;
}

static int device_cmd(int argc, char** argv, bool print_out) {
  if (argc < 3) {
    usage(argv[0]);
    return -1;
  }

  const char* path = argv[2];
  char new_path[32];
  int id = -1;
  if (sscanf(path, "%u", &id) == 1) {
    if (snprintf(new_path, sizeof(new_path), "/dev/class/i2c/%03u", id) >= 0) {
      path = new_path;
    }
  }

  fbl::unique_fd fd(open(path, O_RDWR));
  if (!fd) {
    printf("%s: %s\n", argv[2], strerror(errno));
    usage(argv[0]);
    return -1;
  }

  zx_handle_t svc;
  if ((fdio_get_service_handle(fd.release(), &svc) != ZX_OK)) {
    printf("%s: get service handle failed\n", argv[2]);
    usage(argv[0]);
    return -1;
  }

  zx::channel channel(svc);
  fidl::WireSyncClient<fuchsia_hardware_i2c::Device> client(std::move(channel));

  zx_status_t status = ZX_OK;

  switch (argv[1][0]) {
    case 'w': {
      if (argc < 4) {
        usage(argv[0]);
        return -1;
      }

      size_t n_write_bytes = argc - 3;
      auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
      status = convert_args(&argv[3], n_write_bytes, write_buffer.get());
      if (status != ZX_OK) {
        usage(argv[0]);
        return status;
      }

      status =
          write_bytes(std::move(client), cpp20::span<uint8_t>(write_buffer.get(), n_write_bytes));
      if (status == ZX_OK && print_out) {
        printf("Write: ");
        for (size_t i = 0; i < n_write_bytes; ++i) {
          printf("0x%02X ", write_buffer[i]);
        }
        printf("\n");
      }
      break;
    }

    case 'r': {
      if (argc < 4) {
        usage(argv[0]);
        return -1;
      }

      size_t n_write_bytes = argc - 3;
      auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
      status = convert_args(&argv[3], n_write_bytes, write_buffer.get());
      if (status != ZX_OK) {
        usage(argv[0]);
        return status;
      }

      auto byte =
          read_byte(std::move(client), cpp20::span<uint8_t>(write_buffer.get(), n_write_bytes));
      if (byte.is_ok() && print_out) {
        printf("Read from");
        for (size_t i = 0; i < n_write_bytes; ++i) {
          printf(" 0x%02X", write_buffer[i]);
        }
        printf(": 0x%02X\n", byte.value());
      }
      break;
    }

    case 't': {
      if (argc < 5) {
        usage(argv[0]);
        return -1;
      }

      status = transact(std::move(client), argc, argv);
      break;
    }

    default:
      printf("%c: unrecognized command\n", argv[2][0]);
      usage(argv[0]);
      return -1;
  }
  if (status != ZX_OK) {
    printf("Error %s\n", zx_status_get_string(status));
  }
  return status;
}

static int ping_cmd() {
  const char* c_dir = "/dev/class/i2c";
  DIR* dir = opendir(c_dir);
  if (!dir) {
    printf("Directory %s not found\n", c_dir);
    return -1;
  }

  std::filesystem::path dir_path(c_dir);
  struct dirent* de;
  while ((de = readdir(dir))) {
    std::filesystem::path dev_path = dir_path;
    dev_path /= std::filesystem::path(de->d_name);
    const char* argv[] = {"i2cutil_ping", "r", dev_path.c_str(), "0x00"};
    char** argv_main = (char**)(&argv);
    auto status = device_cmd(std::size(argv), argv_main, false);
    printf("%s: %s\n", dev_path.c_str(), status == ZX_OK ? "OK" : "ERROR");
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage(argv[0]);
    return -1;
  }
  switch (argv[1][0]) {
    case 'w':
    case 'r':
    case 't':
      return device_cmd(argc, argv, true);
      break;
    case 'p':
      return ping_cmd();
      break;

    default:
      usage(argv[0]);
      return -1;
  }

  return 0;
}
