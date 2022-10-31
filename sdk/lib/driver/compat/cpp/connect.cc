// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver/compat/cpp/connect.h>

namespace compat {

void FindDirectoryEntries(fidl::ClientEnd<fuchsia_io::Directory> dir,
                          async_dispatcher_t* dispatcher, EntriesCallback cb) {
  auto client = fidl::WireSharedClient<fuchsia_io::Directory>(std::move(dir), dispatcher);
  auto copy = client.Clone();
  // NOTE: It would be nicer to call Watch, but that is not supported in the component's
  // VFS implementation.
  client->ReadDirents(fuchsia_io::wire::kMaxBuf)
      .Then([client = std::move(copy), cb = std::move(cb)](
                fidl::WireUnownedResult<::fuchsia_io::Directory::ReadDirents>& result) mutable {
        // The format of the packed dirent structure, taken from io.fidl.
        struct dirent {
          // Describes the inode of the entry.
          uint64_t ino;
          // Describes the length of the dirent name in bytes.
          uint8_t size;
          // Describes the type of the entry. Aligned with the
          // POSIX d_type values. Use `DIRENT_TYPE_*` constants.
          uint8_t type;
          // Unterminated name of entry.
          char name[0];
        } __PACKED;

        if (!result.ok()) {
          cb(zx::error(result.status()));
          return;
        }

        size_t index = 0;
        auto& dirents = result->dirents;

        std::vector<std::string> names;

        while (index + sizeof(dirent) < dirents.count()) {
          auto packed_entry = reinterpret_cast<const dirent*>(&result->dirents[index]);
          size_t packed_entry_size = sizeof(dirent) + packed_entry->size;
          if (index + packed_entry_size > dirents.count()) {
            break;
          }
          names.emplace_back(packed_entry->name, packed_entry->size);
          index += packed_entry_size;
        }

        cb(zx::ok(std::move(names)));
      });
}

void ConnectToParentDevices(async_dispatcher_t* dispatcher, const driver::Namespace* ns,
                            ConnectCallback cb) {
  std::vector<ParentDevice> devices;

  auto result = ns->Connect<fuchsia_io::Directory>(fuchsia_driver_compat::Service::Name);

  if (result.is_error()) {
    cb(result.take_error());
    return;
  }

  FindDirectoryEntries(
      std::move(result.value()), dispatcher,
      [ns, cb = std::move(cb)](zx::result<std::vector<std::string>> entries) mutable {
        if (entries.is_error()) {
          cb(entries.take_error());
          return;
        }

        std::vector<ParentDevice> devices;
        for (auto& name : entries.value()) {
          if (name == ".") {
            continue;
          }
          auto result = ns->Connect<fuchsia_driver_compat::Device>(
              std::string(fuchsia_driver_compat::Service::Name)
                  .append("/")
                  .append(name)
                  .append("/device")
                  .c_str());
          if (result.is_error()) {
            cb(result.take_error());
            return;
          }

          devices.push_back(ParentDevice{
              .name = std::move(name),
              .client = std::move(result.value()),
          });
        }
        cb(zx::ok(std::move(devices)));
      });
}

}  // namespace compat
