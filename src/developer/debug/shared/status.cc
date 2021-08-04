// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/status.h"

#include <lib/syslog/cpp/macros.h>
#include <string.h>

namespace debug {

#if defined(__Fuchsia__)

// Map some Fuchsia errors to their cross-platform equivalents. Resets the status to 0 if a
// non-platform error was generated.
Status::Type MapFuchsiaError(zx_status_t* status) {
  switch (*status) {
    case ZX_ERR_NOT_SUPPORTED:
      *status = 0;
      return Status::kNotSupported;
    case ZX_ERR_NOT_FOUND:
      *status = 0;
      return Status::kNotFound;
    case ZX_ERR_ALREADY_EXISTS:
      *status = 0;
      return Status::kAlreadyExists;
    case ZX_ERR_NO_RESOURCES:
      *status = 0;
      return Status::kNoResources;
    default:
      return Status::kPlatformError;
  }
}

Status ZxStatus(zx_status_t s) {
  if (s == ZX_OK)
    return Status();

  // For cross-platform errors, this use the string provided by the system but only set the
  // cross-platform error type. It might be nice to have both a cross-platform type and a
  // platform-specific error code set, but that may require using std::optional for the
  // platform_error.
  const char* msg = zx_status_get_string(s);
  Status::Type type = MapFuchsiaError(&s);
  return Status(Status::InternalValues(), type, static_cast<int64_t>(s), msg);
}

Status ZxStatus(zx_status_t s, std::string msg) {
  if (s == ZX_OK)
    return Status();
  Status::Type type = MapFuchsiaError(&s);
  return Status(Status::InternalValues(), type, static_cast<int64_t>(s), std::move(msg));
}

#else

// Map some errno values to their cross-platform equivalents. Resets the status to 0 if a
// non-platform error was generated.
Status::Type MapErrnoError(int* en) {
  switch (*en) {
    case ENOENT:
      *en = 0;
      return Status::kNotFound;
    case EEXIST:
      *en = 0;
      return Status::kAlreadyExists;
    case ENOTSUP:
      *en = 0;
      return Status::kNotSupported;
    default:
      return Status::kPlatformError;
  }
}

Status ErrnoStatus(int en) {
  if (en == 0)
    return Status();
  Status::Type type = MapErrnoError(&en);
  return Status(Status::InternalValues(), type, static_cast<int64_t>(en), strerror(en));
}

Status ErrnoStatus(int en, std::string msg) {
  if (en == 0)
    return Status();
  Status::Type type = MapErrnoError(&en);
  return Status(Status::InternalValues(), type, static_cast<int64_t>(en), std::move(msg));
}

#endif

Status::Status(std::string msg) : type_(kGenericError), message_(std::move(msg)) {}

Status::Status(Type t, std::string msg) : type_(t), message_(std::move(msg)) {
  FX_DCHECK(t != kPlatformError && t != kSuccess && t != kLast);
}

Status::Status(InternalValues tag, Type t, uint64_t pe, std::string msg)
    : type_(t), platform_error_(pe), message_(std::move(msg)) {
  FX_DCHECK(t != kLast);
  FX_DCHECK(t == kPlatformError || pe == 0);  // |pe| should be 0 for anything but platform errors.
}

}  // namespace debug

std::ostream& operator<<(std::ostream& out, const debug::Status& status) {
  if (status.ok()) {
    return out << "Status(OK)";
  }
  if (status.type() == debug::Status::kPlatformError) {
    return out << "Status(platform error = " << status.platform_error() << ", \""
               << status.message() << "\")";
  }
  // TODO: it might be nice to have a stringified version of Status::Type. For now just use an
  // integer.
  return out << "Status(" << static_cast<uint32_t>(status.type()) << ", \"" << status.message()
             << "\")";
}
