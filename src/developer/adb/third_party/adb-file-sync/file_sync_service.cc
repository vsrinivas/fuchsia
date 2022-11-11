/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define TRACE_TAG SYNC
#include "file_sync_service.h"
// #include "sysdeps.h"
#include <dirent.h>
#include <errno.h>

// #include <linux/xattr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
// #include <sys/xattr.h>
#include <unistd.h>
#include <utime.h>
// #include <android-base/file.h>
// #include <android-base/stringprintf.h>
// #include <android-base/strings.h>
// #include <private/android_filesystem_config.h>
// #include <private/android_logger.h>
// #include <selinux/android.h>
// #include "adb.h"
// #include "adb_io.h"
// #include "adb_trace.h"
// #include "adb_utils.h"
// #include "security_log_tags.h"
// #include "sysdeps/errno.h"
// using android::base::StringPrintf;

#include <fidl/fuchsia.io/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <vector>

#include "adb-file-sync-base.h"
#include "util.h"

/*
static bool should_use_fs_config(const std::string& path) {
    // TODO: use fs_config to configure permissions on /data.
    return android::base::StartsWith(path, "/system/") ||
           android::base::StartsWith(path, "/vendor/") ||
           android::base::StartsWith(path, "/oem/");
}
static bool update_capabilities(const char* path, uint64_t capabilities) {
    if (capabilities == 0) {
        // Ensure we clean up in case the capabilities weren't 0 in the past.
        removexattr(path, XATTR_NAME_CAPS);
        return true;
    }
    vfs_cap_data cap_data = {};
    cap_data.magic_etc = VFS_CAP_REVISION | VFS_CAP_FLAGS_EFFECTIVE;
    cap_data.data[0].permitted = (capabilities & 0xffffffff);
    cap_data.data[0].inheritable = 0;
    cap_data.data[1].permitted = (capabilities >> 32);
    cap_data.data[1].inheritable = 0;
    return setxattr(path, XATTR_NAME_CAPS, &cap_data, sizeof(cap_data), 0) != -1;
}
static bool secure_mkdirs(fidl::WireSyncClient<fuchsia_io::Directory>& parent,
                          const std::string& path) {
  // uid_t uid = -1;
  // gid_t gid = -1;
  // unsigned int mode = 0775;
  // uint64_t capabilities = 0;
  // if (path[0] != '/') {
  //   return false;
  // }
  auto path_components = split_string(path, "/");
  // std::string partial_path;
  fidl::WireSyncClient<fuchsia_io::Directory>& cur = parent;
  for (const auto& path_component : path_components) {
    if (path_component.empty()) {
      // ignore empty path component
      continue;
    }
    // if (partial_path.back() != OS_PATH_SEPARATOR) {
    //   partial_path += OS_PATH_SEPARATOR;
    // }
    // partial_path += path_component;
    // if (should_use_fs_config(partial_path)) {
    //   fs_config(partial_path.c_str(), 1, nullptr, &uid, &gid, &mode, &capabilities);
    // }
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      FX_LOGS(ERROR) << "Could not create endpoints " << endpoints.error_value();
      return false;
    }
    if (auto open = cur->Open(
            fuchsia_io::OpenFlags::kCreate | fuchsia_io::OpenFlags::kRightReadable,
            fuchsia_io::kModeTypeDirectory, fidl::StringView::FromExternal(path_component),
            fidl::ServerEnd<fuchsia_io::Node>(endpoints->server.TakeChannel()));
        !open.ok()) {
      FX_LOGS(ERROR) << "Could not open " << open.error();
      return false;
    }
    cur = fidl::WireSyncClient<fuchsia_io::Directory>(std::move(endpoints->client));

    // if (chown(partial_path.c_str(), uid, gid) == -1)
    //   return false;
    // Not all filesystems support setting SELinux labels. http://b/23530370.
    // selinux_android_restorecon(partial_path.c_str(), 0);
    // if (!update_capabilities(partial_path.c_str(), capabilities))
    //   return false;
  }
  return true;
}
*/
static bool do_lstat_v1(zx::socket& socket, const std::vector<std::string>& path,
                        fidl::WireSyncClient<fuchsia_io::Directory>& component) {
  syncmsg msg = {};
  msg.stat_v1.id = ID_LSTAT_V1;

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  if (endpoints.is_error()) {
    FX_LOGS(ERROR) << "Failed to create endpoints " << endpoints.error_value();
    return false;
  }

  if (!path.empty()) {
    auto result = component->Open(fuchsia_io::OpenFlags::kRightReadable, 0,
                                  fidl::StringView::FromExternal(ConcatenateRelativePath(path)),
                                  std::move(endpoints->server));
    if (!result.ok()) {
      FX_LOGS(ERROR) << "Failed to open file " << result.error();
      return false;
    }
  }

  fuchsia_io::wire::NodeAttributes attr;
  if (path.empty()) {
    auto result = component->GetAttr();
    if (!result.ok()) {
      FX_LOGS(ERROR) << "GetAttr failed with " << result.error();
      return false;
    }
    attr = result.Unwrap()->attributes;
  } else {
    auto result = fidl::WireCall<fuchsia_io::Node>(endpoints->client)->GetAttr();
    if (!result.ok()) {
      FX_LOGS(ERROR) << "GetAttr failed with " << result.error();
      return false;
    }
    attr = result.Unwrap()->attributes;
  }
  msg.stat_v1.mode = attr.mode;
  msg.stat_v1.size = static_cast<uint32_t>(attr.storage_size);
  msg.stat_v1.time = static_cast<uint32_t>(attr.modification_time);
  return WriteFdExactly(socket, &msg.stat_v1, sizeof(msg.stat_v1));
}
static bool do_stat_v2(zx::socket& socket, uint32_t id, const std::vector<std::string>& path,
                       fidl::WireSyncClient<fuchsia_io::Directory>& component) {
  syncmsg msg = {};
  msg.stat_v2.id = id;

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  if (endpoints.is_error()) {
    FX_LOGS(ERROR) << "Failed to create endpoints " << endpoints.error_value();
    return false;
  }

  if (!path.empty()) {
    auto result = component->Open(fuchsia_io::OpenFlags::kRightReadable, 0,
                                  fidl::StringView::FromExternal(ConcatenateRelativePath(path)),
                                  std::move(endpoints->server));
    if (!result.ok()) {
      FX_LOGS(ERROR) << "Failed to open file " << result.error();
      return false;
    }
  }

  zx_status_t status;
  fuchsia_io::wire::NodeAttributes attr;
  if (path.empty()) {
    auto result = component->GetAttr();
    status = result.status();
    if (result.ok()) {
      attr = result->attributes;
    }
  } else {
    auto result = fidl::WireCall<fuchsia_io::Node>(endpoints->client)->GetAttr();
    status = result.status();
    if (result.ok()) {
      attr = result->attributes;
    }
  }
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "GetAttr failed with " << status;
    msg.stat_v2.error = status;
  } else {
    msg.stat_v2.dev = attr.id;
    // msg.stat_v2.ino = st.st_ino;
    msg.stat_v2.mode = attr.mode;
    msg.stat_v2.nlink = static_cast<uint32_t>(attr.link_count);
    // msg.stat_v2.uid = st.st_uid;
    // msg.stat_v2.gid = st.st_gid;
    msg.stat_v2.size = attr.storage_size;
    // msg.stat_v2.atime = st.st_atime;
    msg.stat_v2.mtime = attr.modification_time;
    // msg.stat_v2.ctime = st.st_ctime;
  }
  return WriteFdExactly(socket, &msg.stat_v2, sizeof(msg.stat_v2));
}

static bool do_list(zx::socket& socket, const std::vector<std::string>& path,
                    fidl::WireSyncClient<fuchsia_io::Directory>& component) {
  struct dirent_t {
    // Describes the inode of the entry.
    uint64_t ino;
    // Describes the length of the dirent name in bytes.
    uint8_t size;
    // Describes the type of the entry. Aligned with the
    // POSIX d_type values. Use `DirentType` constants.
    uint8_t type;
    // Unterminated name of entry.
  } __PACKED;

  syncmsg msg;
  msg.dent.id = ID_DENT;

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  if (endpoints.is_error()) {
    FX_LOGS(ERROR) << "Failed to create endpoints " << endpoints.error_value();
    return false;
  }
  if (!path.empty()) {
    if (auto open = component->Open(fuchsia_io::OpenFlags::kRightReadable, 0,
                                    fidl::StringView::FromExternal(ConcatenateRelativePath(path)),
                                    std::move(endpoints->server));
        !open.ok()) {
      FX_LOGS(ERROR) << "Failed to open file " << open.error();
      return false;
    }
  }
  fidl::WireSyncClient<fuchsia_io::Directory> directory(
      fidl::ClientEnd<fuchsia_io::Directory>(endpoints->client.TakeChannel()));
  auto& dir_ptr = path.empty() ? component : directory;

  if (auto rewind = dir_ptr->Rewind(); !rewind.ok()) {
    FX_LOGS(ERROR) << "Rewind failed " << rewind.error();
    goto done;
  }
  while (true) {
    auto result = dir_ptr->ReadDirents(fuchsia_io::kMaxBuf);
    if (!result.ok()) {
      FX_LOGS(ERROR) << "ReadDirents failed with " << result.error();
      goto done;
    }
    if (result->dirents.empty()) {
      break;
    }

    auto it = result->dirents.begin();
    while (true) {
      if (static_cast<size_t>(std::distance(it, result->dirents.end())) < sizeof(dirent_t)) {
        break;
      }
      const auto* dent = reinterpret_cast<dirent_t*>(&*it);
      if (static_cast<size_t>(std::distance(it, result->dirents.end())) <
          sizeof(dirent_t) + dent->size) {
        break;
      }

      std::string name(it + sizeof(dirent_t), it + sizeof(dirent_t) + dent->size);

      auto endpoints = fidl::CreateEndpoints<fuchsia_io::File>();
      fidl::WireSyncClient<fuchsia_io::File> file(std::move(endpoints->client));
      if (endpoints.is_error()) {
        FX_LOGS(ERROR) << "Failed to create channel " << endpoints.status_value();
        goto increment;
      }
      if (auto open = dir_ptr->Open(
              fuchsia_io::OpenFlags::kRightReadable, 0, fidl::StringView::FromExternal(name),
              fidl::ServerEnd<fuchsia_io::Node>(endpoints->server.TakeChannel()));
          !open.ok()) {
        FX_LOGS(ERROR) << "Failed to open file " << open.error();
        goto increment;
      }
      if (auto attr = file->GetAttr(); !attr.ok()) {
        FX_LOGS(ERROR) << "GetAttr failed " << result.error();
        goto increment;
      } else {
        msg.dent.mode = attr->attributes.mode;
        msg.dent.size = static_cast<uint32_t>(attr->attributes.storage_size);
        msg.dent.time = static_cast<uint32_t>(attr->attributes.modification_time);
        msg.dent.namelen = static_cast<uint32_t>(name.length());
      }

      if (!WriteFdExactly(socket, &msg.dent, sizeof(msg.dent)) ||
          !WriteFdExactly(socket, name.data(), dent->size)) {
        return false;
      }

    increment:
      // Increment
      if (static_cast<size_t>(std::distance(it, result->dirents.end())) >
          sizeof(dirent_t) + dent->size) {
        std::advance(it, sizeof(dirent_t) + dent->size);
      } else {
        break;
      }
    }
  }
done:
  msg.dent.id = ID_DONE;
  msg.dent.mode = 0;
  msg.dent.size = 0;
  msg.dent.time = 0;
  msg.dent.namelen = 0;
  return WriteFdExactly(socket, &msg.dent, sizeof(msg.dent));
}
// Make sure that SendFail from adb_io.cpp isn't accidentally used in this file.
// #pragma GCC poison SendFail
static bool SendSyncFail(zx::socket& socket, const std::string& reason) {
  // D("sync: failure: %s", reason.c_str());
  syncmsg msg;
  msg.data.id = ID_FAIL;
  msg.data.size = static_cast<uint32_t>(reason.size());
  return WriteFdExactly(socket, &msg.data, sizeof(msg.data)) &&
         WriteFdExactly(socket, reason.c_str(), reason.size());
}
// static bool SendSyncFailErrno(int fd, const std::string& reason) {
//     return SendSyncFail(fd, StringPrintf("%s: %s", reason.c_str(), strerror(errno)));
// }
static bool handle_send_file(zx::socket& socket, const std::vector<std::string>& path,
                             fidl::WireSyncClient<fuchsia_io::Directory>& component, uid_t uid,
                             gid_t gid, uint64_t capabilities, mode_t mode,
                             std::vector<uint8_t>& buffer, bool do_unlink) {
  syncmsg msg;
  // unsigned int timestamp = 0;
  // __android_log_security_bswrite(SEC_TAG_ADB_SEND_FILE, path);
  // int fd = adb_open_mode(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
  // if (fd < 0 && errno == ENOENT) {
  //     if (!secure_mkdirs(android::base::Dirname(path))) {
  //         SendSyncFailErrno(s, "secure_mkdirs failed");
  //         goto fail;
  //     }
  //     fd = adb_open_mode(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
  // }
  // if (fd < 0 && errno == EEXIST) {
  //     fd = adb_open_mode(path, O_WRONLY | O_CLOEXEC, mode);
  // }
  // if (fd < 0) {
  //     SendSyncFailErrno(s, "couldn't create file");
  //     goto fail;
  // } else {
  //     if (fchown(fd, uid, gid) == -1) {
  //         SendSyncFailErrno(s, "fchown failed");
  //         goto fail;
  //     }
  //     // Not all filesystems support setting SELinux labels. http://b/23530370.
  //     selinux_android_restorecon(path, 0);
  //     // fchown clears the setuid bit - restore it if present.
  //     // Ignore the result of calling fchmod. It's not supported
  //     // by all filesystems, so we don't check for success. b/12441485
  //     fchmod(fd, mode);
  // }
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::File>();
  fidl::WireSyncClient<fuchsia_io::File> file(std::move(endpoints->client));
  if (endpoints.is_error()) {
    FX_LOGS(ERROR) << "Failed to create channel " << endpoints.status_value();
    SendSyncFail(socket, "Create endpoints failed");
    goto fail;
  }
  if (auto open = component->Open(
          fuchsia_io::OpenFlags::kRightWritable | fuchsia_io::OpenFlags::kCreate |
              fuchsia_io::OpenFlags::kTruncate | fuchsia_io::OpenFlags::kNotDirectory,
          fuchsia_io::kModeTypeFile, fidl::StringView::FromExternal(ConcatenateRelativePath(path)),
          fidl::ServerEnd<fuchsia_io::Node>(endpoints->server.TakeChannel()));
      !open.ok()) {
    SendSyncFail(socket, "Open failed");
    FX_LOGS(INFO) << "Open failed " << open.error();
    goto fail;
  }
  while (true) {
    if (!ReadFdExactly(socket, &msg.data, sizeof(msg.data))) {
      FX_LOGS(ERROR) << "read failed";
      goto fail;
    }
    if (msg.data.id != ID_DATA) {
      if (msg.data.id == ID_DONE) {
        // timestamp = msg.data.size;
        break;
      }
      SendSyncFail(socket, "invalid data message");
      goto abort;
    }
    if (msg.data.size > buffer.size()) {  // TODO: resize buffer?
      SendSyncFail(socket, "oversize data message");
      goto abort;
    }
    if (!ReadFdExactly(socket, buffer.data(), msg.data.size)) {
      SendSyncFail(socket, "read failed");
      goto abort;
    }
    uint64_t write_len = 0;
    while (write_len < msg.data.size) {
      auto cur_len = std::min(fuchsia_io::kMaxTransferSize, msg.data.size - write_len);
      if (auto write = file->Write(
              fidl::VectorView<uint8_t>::FromExternal(buffer.data() + write_len, cur_len));
          !write.ok() || write->is_error()) {
        FX_LOGS(ERROR) << "File Write failed "
                       << (write.ok() ? write->error_value() : write.status());
        SendSyncFail(socket, "File Write failed");
        goto abort;
      }
      write_len += cur_len;
    }
  }
  if (auto close = file->Close(); !close.ok() || close->is_error()) {
    FX_LOGS(ERROR) << "File Close failed " << (close.ok() ? close->error_value() : close.status());
  }
  // if (!update_capabilities(path, capabilities)) {
  //     SendSyncFailErrno(s, "update_capabilities failed");
  //     goto fail;
  // }
  // utimbuf u;
  // u.actime = timestamp;
  // u.modtime = timestamp;
  // utime(path.c_str(), &u);
  msg.status.id = ID_OKAY;
  msg.status.msglen = 0;
  return WriteFdExactly(socket, &msg.status, sizeof(msg.status));
fail:
  // FX_LOGS(ERROR) << "fail";
  // If there's a problem on the device, we'll send an ID_FAIL message and
  // close the socket. Unfortunately the kernel will sometimes throw that
  // data away if the other end keeps writing without reading (which is
  // the case with old versions of adb). To maintain compatibility, keep
  // reading and throwing away ID_DATA packets until the other side notices
  // that we've reported an error.
  while (true) {
    if (!ReadFdExactly(socket, &msg.data, sizeof(msg.data)))
      goto fail;
    if (msg.data.id == ID_DONE) {
      goto abort;
    } else if (msg.data.id != ID_DATA) {
      char id[5];
      memcpy(id, &msg.data.id, sizeof(msg.data.id));
      id[4] = '\0';
      // D("handle_send_fail received unexpected id '%s' during failure", id);
      goto abort;
    }
    if (msg.data.size > buffer.size()) {
      // D("handle_send_fail received oversized packet of length '%u' during failure",
      //   msg.data.size);
      goto abort;
    }
    if (!ReadFdExactly(socket, &buffer[0], msg.data.size))
      goto abort;
  }
abort:
  auto close = file->Close();
  if (!close.ok() || close->is_error()) {
    FX_LOGS(ERROR) << "File Close failed " << (close.ok() ? close->error_value() : close.status());
  }
  // TODO: Currently do not support links.
  // if (do_unlink) adb_unlink(path);
  return false;
}
/*
#if defined(_WIN32)
extern bool handle_send_link(int s, const std::string& path, std::vector<char>& buffer)
__attribute__((error("no symlinks on Windows"))); #else static bool handle_send_link(int s, const
std::string& path, std::vector<char>& buffer) { syncmsg msg; unsigned int len; int ret; if
(!ReadFdExactly(s, &msg.data, sizeof(msg.data))) return false; if (msg.data.id != ID_DATA) {
        SendSyncFail(s, "invalid data message: expected ID_DATA");
        return false;
    }
    len = msg.data.size;
    if (len > buffer.size()) { // TODO: resize buffer?
        SendSyncFail(s, "oversize data message");
        return false;
    }
    if (!ReadFdExactly(s, &buffer[0], len)) return false;
    ret = symlink(&buffer[0], path.c_str());
    if (ret && errno == ENOENT) {
        if (!secure_mkdirs(android::base::Dirname(path))) {
            SendSyncFailErrno(s, "secure_mkdirs failed");
            return false;
        }
        ret = symlink(&buffer[0], path.c_str());
    }
    if (ret) {
        SendSyncFailErrno(s, "symlink failed");
        return false;
    }
    if (!ReadFdExactly(s, &msg.data, sizeof(msg.data))) return false;
    if (msg.data.id == ID_DONE) {
        msg.status.id = ID_OKAY;
        msg.status.msglen = 0;
        if (!WriteFdExactly(s, &msg.status, sizeof(msg.status))) return false;
    } else {
        SendSyncFail(s, "invalid data message: expected ID_DONE");
        return false;
    }
    return true;
}
#endif
*/
static bool do_send(zx::socket& socket, std::vector<std::string>& spec,
                    std::vector<uint8_t>& buffer,
                    fidl::WireSyncClient<fuchsia_io::Directory>& component) {
  // 'spec' is of the form "/some/path,0755". Break it up.
  size_t comma = spec.back().find_last_of(',');
  if (comma == std::string::npos) {
    SendSyncFail(socket, "missing , in ID_SEND");
    return false;
  }
  mode_t mode = static_cast<mode_t>(strtoul(spec.back().substr(comma + 1).c_str(), nullptr, 0));
  spec.back() = spec.back().substr(0, comma);
  // Don't delete files before copying if they are not "regular" or symlinks.
  // struct stat st;
  // TODO: Currently do not support links.
  // bool do_unlink = (lstat(path.c_str(), &st) == -1) || S_ISREG(st.st_mode) ||
  // S_ISLNK(st.st_mode); if (do_unlink) {
  //     adb_unlink(path.c_str());
  // }
  // if (S_ISLNK(mode)) {
  //     return handle_send_link(s, path.c_str(), buffer);
  // }
  // Copy user permission bits to "group" and "other" permissions.
  mode &= 0777;
  mode |= ((mode >> 3) & 0070);
  mode |= ((mode >> 3) & 0007);
  uid_t uid = -1;
  gid_t gid = -1;
  uint64_t capabilities = 0;
  // if (should_use_fs_config(path)) {
  //     unsigned int broken_api_hack = mode;
  //     fs_config(path.c_str(), 0, nullptr, &uid, &gid, &broken_api_hack, &capabilities);
  //     mode = broken_api_hack;
  // }
  return handle_send_file(socket, spec, component, uid, gid, capabilities, mode, buffer, false);
}
static bool do_recv(zx::socket& socket, const std::vector<std::string>& path,
                    fidl::WireSyncClient<fuchsia_io::Directory>& component) {
  // __android_log_security_bswrite(SEC_TAG_ADB_RECV_FILE, path);

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::File>();
  if (endpoints.is_error()) {
    FX_LOGS(ERROR) << "Could not create endpoint " << endpoints.status_value();
    return false;
  }
  if (auto open =
          component->Open(fuchsia_io::OpenFlags::kRightReadable, fuchsia_io::kModeTypeFile,
                          fidl::StringView::FromExternal(ConcatenateRelativePath(path)),
                          fidl::ServerEnd<fuchsia_io::Node>(endpoints->server.TakeChannel()));
      !open.ok()) {
    FX_LOGS(INFO) << "Open failed " << open.error();
    SendSyncFail(socket, "open failed");
    return false;
  }
  fidl::WireSyncClient<fuchsia_io::File> file(std::move(endpoints->client));

  syncmsg msg;
  msg.data.id = ID_DATA;
  while (true) {
    auto result = file->Read(fuchsia_io::kMaxBuf);
    if (!result.ok() || result->is_error()) {
      FX_LOGS(ERROR) << "File Read failed "
                     << (result.ok() ? result->error_value() : result.status());
      return false;
    }
    msg.data.size = static_cast<uint32_t>(result->value()->data.count());
    if (msg.data.size == 0) {
      break;
    }
    if (!WriteFdExactly(socket, &msg.data, sizeof(msg.data)) ||
        !WriteFdExactly(socket, result->value()->data.data(), msg.data.size)) {
      if (auto close = file->Close(); !close.ok() || close->is_error()) {
        FX_LOGS(ERROR) << "Failed to close file "
                       << (close.ok() ? close->error_value() : close.status());
      }
      return false;
    }
  }
  if (auto close = file->Close(); !close.ok() || close->is_error()) {
    FX_LOGS(ERROR) << "Failed to close file "
                   << (close.ok() ? close->error_value() : close.status());
  }
  msg.data.id = ID_DONE;
  msg.data.size = 0;
  return WriteFdExactly(socket, &msg.data, sizeof(msg.data));
}
static const char* sync_id_to_name(uint32_t id) {
  switch (id) {
    case ID_LSTAT_V1:
      return "lstat_v1";
    case ID_LSTAT_V2:
      return "lstat_v2";
    case ID_STAT_V2:
      return "stat_v2";
    case ID_LIST:
      return "list";
    case ID_SEND:
      return "send";
    case ID_RECV:
      return "recv";
    case ID_QUIT:
      return "quit";
    default:
      return "???";
  }
}

static bool handle_sync_command(void* ctx, zx::socket& socket, std::vector<uint8_t>& buffer) {
  zx_signals_t pending;
  auto status =
      socket.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED, zx::time::infinite(), &pending);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Socket wait failed " << status;
    return false;
  }

  if (pending & ZX_SOCKET_PEER_CLOSED) {
    FX_LOGS(DEBUG) << "Peer closed";
    return false;
  }

  // ATRACE_CALL();
  SyncRequest request;
  if (!ReadFdExactly(socket, &request, sizeof(request))) {
    SendSyncFail(socket, "command read failure");
    return false;
  }
  size_t path_length = request.path_length;
  if (path_length > 1024) {
    SendSyncFail(socket, "path too long");
    return false;
  }
  char name[1025];
  if (!ReadFdExactly(socket, name, path_length)) {
    SendSyncFail(socket, "filename read failure");
    return false;
  }
  name[path_length] = 0;
  std::vector<std::string> path;
  fidl::WireSyncClient<fuchsia_io::Directory> component;
  auto get_component = [&](void* ctx, const std::string& name) {
    // Connect to component
    auto dir = static_cast<adb_file_sync::AdbFileSyncBase*>(ctx)->ConnectToComponent(name, &path);
    if (dir.is_error()) {
      FX_LOGS(ERROR) << "Could not connect to component " << name;
      return false;
    }
    component.Bind(fidl::ClientEnd<fuchsia_io::Directory>(std::move(dir.value())));
    return true;
  };
  std::string id_name = sync_id_to_name(request.id);
  // std::string trace_name = StringPrintf("%s(%s)", id_name.c_str(), name);
  // ATRACE_NAME(trace_name.c_str());
  FX_LOGS(DEBUG) << "sync: " << id_name.c_str() << "('" << name << "')";
  switch (request.id) {
    case ID_LSTAT_V1:
      if (!get_component(ctx, name) || !do_lstat_v1(socket, path, component))
        return false;
      break;
    case ID_LSTAT_V2:
    case ID_STAT_V2:
      if (!get_component(ctx, name) || !do_stat_v2(socket, request.id, path, component))
        return false;
      break;
    case ID_LIST:
      if (!get_component(ctx, name) || !do_list(socket, path, component))
        return false;
      break;
    case ID_SEND:
      if (!get_component(ctx, name) || !do_send(socket, path, buffer, component))
        return false;
      break;
    case ID_RECV:
      if (!get_component(ctx, name) || !do_recv(socket, path, component))
        return false;
      break;
    case ID_QUIT:
      return false;
    default:
      SendSyncFail(socket, "unknown command");  // StringPrintf("unknown command %08x",
                                                // request.id));
      return false;
  }
  return true;
}

void file_sync_service(void* ctx, zx::socket socket) {
  std::vector<uint8_t> buffer(SYNC_DATA_MAX);
  while (handle_sync_command(ctx, socket, buffer)) {
  }
  socket.signal_peer(0, ZX_ERR_PEER_CLOSED);
  FX_LOGS(DEBUG) << "sync: done";
}
