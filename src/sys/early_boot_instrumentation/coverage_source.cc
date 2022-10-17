// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/early_boot_instrumentation/coverage_source.h"

#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/debugdata/c/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/io.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/stdcompat/source_location.h>
#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/service.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <fbl/unique_fd.h>
#include <sdk/lib/vfs/cpp/pseudo_dir.h>
#include <sdk/lib/vfs/cpp/vmo_file.h>

namespace early_boot_instrumentation {
namespace {

constexpr std::string_view kKernelProfRaw = "zircon.elf.profraw";
constexpr std::string_view kKernelSymbolizerLog = "symbolizer.log";

constexpr std::string_view kPhysbootProfRaw = "physboot.profraw";
constexpr std::string_view kPhysbootSymbolizerLog = "symbolizer.log";

struct ExportedFd {
  fbl::unique_fd fd;
  std::string export_name;
};

zx::result<> Export(vfs::PseudoDir& out_dir, cpp20::span<ExportedFd> exported_fds) {
  for (const auto& [fd, export_as] : exported_fds) {
    // Get the underlying vmo of the fd.
    zx::vmo vmo;
    if (auto res = fdio_get_vmo_exact(fd.get(), vmo.reset_and_get_address()); res != ZX_OK) {
      return zx::error(res);
    }
    size_t size = 0;
    if (auto res = vmo.get_size(&size); res != ZX_OK) {
      return zx::error(res);
    }

    auto file = std::make_unique<vfs::VmoFile>(std::move(vmo), size);
    if (auto res = out_dir.AddEntry(export_as, std::move(file)); res != ZX_OK) {
      return zx::error(res);
    }
  }
  return zx::success();
}

// TODO(fxbug.dev/82681): Clean up manual FIDL definitions once there exists a stable way of doing
// this.
struct fuchsia_io_DirectoryOpenRequest {
  FIDL_ALIGNDECL
  fidl_message_header_t hdr;
  uint32_t flags;
  uint32_t mode;
  fidl_string_t path;
  zx_handle_t object;
};

struct OpenData {
  std::string path;
  zx::unowned_channel service_request;
};

zx::result<OpenData> GetOpenData(cpp20::span<uint8_t> message, cpp20::span<zx_handle_t> handles) {
  if (message.size() < sizeof(fuchsia_io_DirectoryOpenRequest)) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }

  if (handles.size() != 1) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  auto* open_rq = reinterpret_cast<fuchsia_io_DirectoryOpenRequest*>(message.data());

  return zx::ok(
      OpenData{.path = std::string(reinterpret_cast<char*>(message.data() + sizeof(*open_rq)),
                                   open_rq->path.size),
               .service_request = zx::unowned_channel(handles[0])});
}

struct PublishedData {
  std::string sink;
  zx::vmo data;
  zx::eventpair token;
  size_t content_size;
};

zx::result<PublishedData> GetPublishedData(cpp20::span<uint8_t> message,
                                           cpp20::span<zx_handle_t> handles) {
  if (message.size() < sizeof(fuchsia_debugdata_PublisherPublishRequestMessage)) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }
  auto* publish_rq =
      reinterpret_cast<fuchsia_debugdata_PublisherPublishRequestMessage*>(message.data());

  if (handles.size() != 2) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }

  zx::vmo data(std::exchange(handles[0], ZX_HANDLE_INVALID));
  size_t size = 0;

  if (auto res = data.get_prop_content_size(&size); res != ZX_OK) {
    FX_LOGS(INFO) << "Failed to obtain vmo content size. Attempting to use vmo size.";
  }

  if (size == 0) {
    if (auto res = data.get_size(&size); res != ZX_OK) {
      FX_LOGS(INFO) << "Failed to obtain vmo size.";
      return zx::error(res);
    }
  }

  return zx::ok(PublishedData{
      .sink = std::string(reinterpret_cast<char*>(message.data()) + sizeof(*publish_rq),
                          publish_rq->data_sink.size),
      .data = std::move(data),
      .token = zx::eventpair(handles[1]),
      .content_size = size,
  });
}

template <typename HandleType>
bool IsSignalled(const HandleType& handle, zx_signals_t signal) {
  zx_signals_t actual = 0;
  auto status = handle.wait_one(signal, zx::time::infinite_past(), &actual);
  return (status == ZX_OK || status == ZX_ERR_TIMED_OUT) && (actual & signal) != 0;
}

struct ChannelMessageInfo {
  uint32_t outstanding_bytes = 0;
  uint32_t outstanding_handles = 0;
};

zx::result<ChannelMessageInfo> GetChannelOutstandingBytesAndHandles(const zx::channel& channel) {
  ChannelMessageInfo info;
  if (auto res = channel.read(0, nullptr, nullptr, 0, 0, &info.outstanding_bytes,
                              &info.outstanding_handles);
      res != ZX_OK && res != ZX_ERR_BUFFER_TOO_SMALL) {
    return zx::error(res);
  }
  return zx::ok(info);
}

// MessageVisitor is a Callable with a signature of (status, bytes, handles).
template <typename MessageVisitor>
void OnEachMessage(zx::unowned_channel& src, MessageVisitor visitor) {
  static_assert(std::is_invocable_v<MessageVisitor, zx_status_t, cpp20::span<uint8_t>,
                                    cpp20::span<zx_handle_t>>);
  if (!src->is_valid()) {
    visitor(ZX_ERR_BAD_HANDLE, cpp20::span<uint8_t>{}, cpp20::span<zx_handle_t>{});
    return;
  }

  std::vector<uint8_t> bytes;
  std::vector<zx_handle_t> handles;
  while (IsSignalled(*src, ZX_CHANNEL_READABLE)) {
    auto res = GetChannelOutstandingBytesAndHandles(*src);
    if (res.is_error()) {
      visitor(res.status_value(), cpp20::span<uint8_t>{}, cpp20::span<zx_handle_t>{});
      break;
    }

    auto [byte_count, handle_count] = *res;
    bytes.resize(byte_count);
    handles.resize(handle_count);
    uint32_t actual_bytes, actual_handles;
    auto status = src->read(0, bytes.data(), handles.data(), byte_count, handle_count,
                            &actual_bytes, &actual_handles);
    visitor(status, cpp20::span(bytes), cpp20::span(handles));
    if (status != ZX_OK) {
      break;
    }
    zx_handle_close_many(handles.data(), handle_count);
  }
}

enum class DataType {
  kDynamic,
  kStatic,
};

// Returns or creates the respective instance for a given |sink_name|.
vfs::PseudoDir& GetOrCreate(std::string_view sink_name, DataType type, SinkDirMap& sink_map) {
  auto it = sink_map.find(sink_name);

  // If it's the first time we see this sink, fill up the base hierarchy:
  //  root
  //    +    /static
  //    +    /dynamic
  if (it == sink_map.end()) {
    it = sink_map.insert(std::make_pair(sink_name, std::make_unique<vfs::PseudoDir>())).first;
    it->second->AddEntry(std::string(kStaticDir), std::make_unique<vfs::PseudoDir>());
    it->second->AddEntry(std::string(kDynamicDir), std::make_unique<vfs::PseudoDir>());
  }

  std::string path(type == DataType::kDynamic ? kDynamicDir : kStaticDir);

  auto& root_dir = *(it->second);
  vfs::internal::Node* node = nullptr;
  // Both subdirs should always be available.
  ZX_ASSERT(root_dir.Lookup(path, &node) == ZX_OK);
  ZX_ASSERT(node->IsDirectory());
  return *reinterpret_cast<vfs::PseudoDir*>(node);
}

}  // namespace

zx::result<> ExposeKernelProfileData(fbl::unique_fd& kernel_data_dir, SinkDirMap& sink_map) {
  std::vector<ExportedFd> exported_fds;

  fbl::unique_fd kernel_profile(openat(kernel_data_dir.get(), kKernelProfRaw.data(), O_RDONLY));
  if (!kernel_profile) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  exported_fds.emplace_back(
      ExportedFd{.fd = std::move(kernel_profile), .export_name = std::string(kKernelFile)});

  fbl::unique_fd kernel_log(openat(kernel_data_dir.get(), kKernelSymbolizerLog.data(), O_RDONLY));
  if (kernel_log) {
    exported_fds.emplace_back(
        ExportedFd{.fd = std::move(kernel_log), .export_name = std::string(kKernelSymbolizerFile)});
  }

  return Export(GetOrCreate(kLlvmSink, DataType::kDynamic, sink_map), exported_fds);
}

zx::result<> ExposePhysbootProfileData(fbl::unique_fd& physboot_data_dir, SinkDirMap& sink_map) {
  std::vector<ExportedFd> exported_fds;

  fbl::unique_fd phys_profile(openat(physboot_data_dir.get(), kPhysbootProfRaw.data(), O_RDONLY));
  if (!phys_profile) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  exported_fds.emplace_back(
      ExportedFd{.fd = std::move(phys_profile), .export_name = std::string(kPhysFile)});

  fbl::unique_fd phys_log(openat(physboot_data_dir.get(), kPhysbootSymbolizerLog.data(), O_RDONLY));
  if (phys_log) {
    exported_fds.emplace_back(
        ExportedFd{.fd = std::move(phys_log), .export_name = std::string(kPhysSymbolizerFile)});
  }

  return Export(GetOrCreate(kLlvmSink, DataType::kStatic, sink_map), exported_fds);
}

SinkDirMap ExtractDebugData(zx::unowned_channel svc_stash) {
  SinkDirMap sink_to_dir;

  // Results from a publish request.
  std::vector<PublishedData> published_data;

  // used for name generation.
  int svc_id = 0;
  int req_id = 0;

  auto on_publish_request = [&sink_to_dir, &svc_id, &req_id](zx_status_t status, auto bytes,
                                                             auto handles) {
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Encountered error status while processing open requests "
                    << zx_status_get_string(status);
      return;
    }

    if (auto published_data_or = GetPublishedData(bytes, handles); published_data_or.is_ok()) {
      auto& [sink, vmo, token, content_size] = *published_data_or;
      DataType published_data_type =
          IsSignalled(token, ZX_EVENTPAIR_PEER_CLOSED) ? DataType::kStatic : DataType::kDynamic;
      auto& dir = GetOrCreate(sink, published_data_type, sink_to_dir);
      std::array<char, ZX_MAX_NAME_LEN> name_buff = {};
      auto name = std::to_string(svc_id) + "-" + std::to_string(req_id);
      if (auto res = vmo.get_property(ZX_PROP_NAME, name_buff.data(), name_buff.size());
          res == ZX_OK) {
        std::string name_prop(name_buff.data());
        if (!name_prop.empty()) {
          name += "." + name_prop;
        }
      }
      dir.AddEntry(std::move(name), std::make_unique<vfs::VmoFile>(std::move(vmo), content_size));
      ++req_id;
    } else {
      FX_LOGS(INFO) << "Encountered error(" << published_data_or.status_string()
                    << " while parsing publish request. Skipping entry.";
    }
  };

  auto on_open_request = [&on_publish_request](zx_status_t status, auto bytes, auto handles) {
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Encountered error status while processing open requests "
                    << zx_status_get_string(status);
      return;
    }

    if (handles.size() != 1) {
      FX_LOGS(INFO) << "Stashed svc contains invalid handle.";
      return;
    }
    if (auto open_data_or = GetOpenData(bytes, handles); open_data_or.is_ok()) {
      auto& [path, service_request] = *open_data_or;
      if (path == fuchsia_debugdata_Publisher_Name) {
        zx::unowned_channel service_request(handles[0]);
        OnEachMessage(service_request, on_publish_request);
      } else {
        FX_LOGS(INFO) << "Encountered open request to unhandled path " << path;
      }
    } else {
      FX_LOGS(INFO) << "Encountered error(" << open_data_or.status_string()
                    << " while parsing open request. Skipping entry.";
    }
  };

  auto on_stashed_svc = [&on_open_request, &req_id, &svc_id](zx_status_t status, auto bytes,
                                                             auto handles) {
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Encountered error status while processing open requests "
                    << zx_status_get_string(status);
      return;
    }
    if (handles.size() != 1) {
      FX_LOGS(INFO) << "No stashed handle on svc stashed channel message. Skipping.";
      return;
    }
    if (bytes.size() < sizeof(fuchsia_boot_SvcStashStoreRequestMessage)) {
      FX_LOGS(INFO) << "SvcStash/Store request message expected, but message too small. Skipping.";
      return;
    }

    auto* req = reinterpret_cast<fuchsia_boot_SvcStashStoreRequestMessage*>(bytes.data());
    // Small verification that the fidl header is what we expect.
    if (req->hdr.magic_number != kFidlWireFormatMagicNumberInitial ||
        req->hdr.ordinal != fuchsia_boot_SvcStashStoreOrdinal) {
      FX_LOGS(INFO) << "SvcStash/Push request message expected, but message header could not be "
                       "verified. Skipping.";
      return;
    }

    zx::unowned_channel stashed_svc(handles[0]);
    OnEachMessage(stashed_svc, on_open_request);
    req_id = 0;
    svc_id++;
  };

  OnEachMessage(svc_stash, on_stashed_svc);
  return sink_to_dir;
}

}  // namespace early_boot_instrumentation
