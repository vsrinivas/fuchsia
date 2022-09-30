// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:zircon/zircon.dart';

/// Helper object representing a View and Viewport creation token pair.
class ViewCreationTokenPair {
  /// Token for a Scenic |ViewCreationToken|. This represents the child endpoint
  /// of the connection between two Flatland instances. For further information,
  /// see: https://fuchsia.dev/reference/fidl/fuchsia.ui.views
  final ViewCreationToken viewToken;

  /// Token for a Scenic |ViewportCreationToken|. This represents the parent
  /// endpoint of the connection between two Flatland instances. For further
  /// information, see: https://fuchsia.dev/reference/fidl/fuchsia.ui.views
  final ViewportCreationToken viewportToken;

  /// Creates a ViewCreationTokenPair with new View and Viewport creation tokens.
  ViewCreationTokenPair() : this._fromChannels(ChannelPair());

  /// Helper constructor to create from a |ChannelPair| of channels.
  ViewCreationTokenPair._fromChannels(ChannelPair channels)
      : assert(channels.status == ZX.OK),
        viewToken = ViewCreationToken(value: channels.first!),
        viewportToken = ViewportCreationToken(value: channels.second!);
}
