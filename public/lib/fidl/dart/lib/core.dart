// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library core;

import 'dart:async';
import 'dart:zircon';
import 'dart:typed_data';

export 'dart:zircon' show Handle, ReadResult, WriteResult, GetSizeResult;

part 'src/core/channel.dart';
part 'src/core/channel_reader.dart';
part 'src/core/errors.dart';
part 'src/core/eventpair.dart';
part 'src/core/socket.dart';
part 'src/core/socket_reader.dart';
part 'src/core/types.dart';
part 'src/core/vmo.dart';
