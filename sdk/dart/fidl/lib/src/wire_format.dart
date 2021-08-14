// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

enum WireFormat {
  v1,
  v2,
}
const WireFormat kWireFormatDefault = WireFormat.v1;

const int kEnvelopeInlineMarker = 1;
const int kEnvelopeOutOfLineMarker = 0;
