// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_MESSAGES_H_
#define SRC_CAMERA_BIN_DEVICE_MESSAGES_H_

namespace Messages {

constexpr auto kDeviceAlreadyBound =
    "This device already has active clients. New clients may connect via existing clients using "
    "the camera3.Device.Rebind method.";

constexpr auto kStreamAlreadyBound =
    "This stream already has active clients. New clients may connect via existing clients using "
    "the camera3.Stream.Rebind method.";

constexpr auto kNoCampingBuffers =
    "The client did not specify BufferCollectionConstraints.min_buffer_count* using its "
    "BufferCollectionToken. The client will not be able to receive frames.";

constexpr auto kMultipleFrameClients =
    "Multiple clients are waiting for frames on the same stream. Only one client will receive any "
    "given frame, so this is unliikely to result in reliable behavior. To transfer frame "
    "ownership, prefer transferring the release fence instead.";

}  // namespace Messages

#endif  // SRC_CAMERA_BIN_DEVICE_MESSAGES_H_
