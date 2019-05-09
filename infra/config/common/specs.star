# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


def _spec(name, protobuf):
    return struct(
        path=name,
        data=proto.to_textpb(protobuf),
    )


specs = struct(spec=_spec, )
