// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/interception_tests/test_library.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "src/lib/fidl_codec/library_loader.h"

// Generated with go/fidlbolt using this text:
// library fidl.examples.echo;
//
// using zx;
//
// [Discoverable]
// protocol Echo {
//   EchoString(string? value) -> (string? response);
//   EchoHandle(zx.handle handle) -> (zx.handle handle);
//   -> OnPong();
// };

std::string echo_service = R"({
  "version": "0.0.1",
  "name": "fidl.examples.echo",
  "library_dependencies": [
    {
      "name": "zx",
      "declarations": {
        "zx/rights": {
          "kind": "bits"
        },
        "zx/ZX_PRIORITY_LOWEST": {
          "kind": "const"
        },
        "zx/ZX_PRIORITY_LOW": {
          "kind": "const"
        },
        "zx/ZX_PRIORITY_DEFAULT": {
          "kind": "const"
        },
        "zx/ZX_PRIORITY_HIGH": {
          "kind": "const"
        },
        "zx/ZX_PRIORITY_HIGHEST": {
          "kind": "const"
        },
        "zx/CHANNEL_MAX_MSG_BYTES": {
          "kind": "const"
        },
        "zx/CHANNEL_MAX_MSG_HANDLES": {
          "kind": "const"
        },
        "zx/MAX_NAME_LEN": {
          "kind": "const"
        },
        "zx/MAX_CPUS": {
          "kind": "const"
        },
        "zx/clock": {
          "kind": "enum"
        },
        "zx/ProfileInfoType": {
          "kind": "enum"
        },
        "zx/stream_seek_origin": {
          "kind": "enum"
        },
        "zx/obj_type": {
          "kind": "enum"
        },
        "zx/handle": {
          "kind": "experimental_resource"
        },
        "zx/bti": {
          "kind": "interface"
        },
        "zx/cache": {
          "kind": "interface"
        },
        "zx/channel": {
          "kind": "interface"
        },
        "zx/clockfuncs": {
          "kind": "interface"
        },
        "zx/cprng": {
          "kind": "interface"
        },
        "zx/debug": {
          "kind": "interface"
        },
        "zx/debuglog": {
          "kind": "interface"
        },
        "zx/event": {
          "kind": "interface"
        },
        "zx/eventpair": {
          "kind": "interface"
        },
        "zx/exception": {
          "kind": "interface"
        },
        "zx/fifo": {
          "kind": "interface"
        },
        "zx/framebuffer": {
          "kind": "interface"
        },
        "zx/futexfuncs": {
          "kind": "interface"
        },
        "zx/guest": {
          "kind": "interface"
        },
        "zx/handlefuncs": {
          "kind": "interface"
        },
        "zx/interrupt": {
          "kind": "interface"
        },
        "zx/iommu": {
          "kind": "interface"
        },
        "zx/ioports": {
          "kind": "interface"
        },
        "zx/job": {
          "kind": "interface"
        },
        "zx/ktrace": {
          "kind": "interface"
        },
        "zx/misc": {
          "kind": "interface"
        },
        "zx/msi": {
          "kind": "interface"
        },
        "zx/mtrace": {
          "kind": "interface"
        },
        "zx/object": {
          "kind": "interface"
        },
        "zx/pager": {
          "kind": "interface"
        },
        "zx/pc": {
          "kind": "interface"
        },
        "zx/pci": {
          "kind": "interface"
        },
        "zx/pmt": {
          "kind": "interface"
        },
        "zx/port": {
          "kind": "interface"
        },
        "zx/process": {
          "kind": "interface"
        },
        "zx/profile": {
          "kind": "interface"
        },
        "zx/resource": {
          "kind": "interface"
        },
        "zx/smc": {
          "kind": "interface"
        },
        "zx/socket": {
          "kind": "interface"
        },
        "zx/stream": {
          "kind": "interface"
        },
        "zx/syscall": {
          "kind": "interface"
        },
        "zx/system": {
          "kind": "interface"
        },
        "zx/task": {
          "kind": "interface"
        },
        "zx/thread": {
          "kind": "interface"
        },
        "zx/timer": {
          "kind": "interface"
        },
        "zx/vcpu": {
          "kind": "interface"
        },
        "zx/vmar": {
          "kind": "interface"
        },
        "zx/vmo": {
          "kind": "interface"
        },
        "zx/SomeLongAnonymousPrefix0": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix1": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix2": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix3": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix4": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix5": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix6": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix7": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix8": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix9": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix10": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix11": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix12": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix13": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix14": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix15": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix16": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix17": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix18": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix19": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix20": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix21": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix22": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix23": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix24": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix25": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix26": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix27": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix28": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix29": {
          "kind": "struct",
          "resource": true
        },
        "zx/HandleInfo": {
          "kind": "struct",
          "resource": true
        },
        "zx/ChannelCallArgs": {
          "kind": "struct",
          "resource": true
        },
        "zx/HandleDisposition": {
          "kind": "struct",
          "resource": true
        },
        "zx/ChannelCallEtcArgs": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix30": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix31": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix32": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix33": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix34": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix35": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix36": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix37": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix38": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix39": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix40": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix41": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix42": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix43": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix44": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix45": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix46": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix47": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix48": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix49": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix50": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix51": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix52": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix53": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix54": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix55": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix56": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix57": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix58": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix59": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix60": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix61": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix62": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix63": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix64": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix65": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix66": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix67": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix68": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix69": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix70": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix71": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix72": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix73": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix74": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix75": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix76": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix77": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix78": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix79": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix80": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix81": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix82": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix83": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix84": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix85": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix86": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix87": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix88": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix89": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix90": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix91": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix92": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix93": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix94": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix95": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix96": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix97": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix98": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix99": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix100": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix101": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix102": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix103": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix104": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix105": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix106": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix107": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix108": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix109": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix110": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix111": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix112": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix113": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix114": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix115": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix116": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix117": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix118": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix119": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix120": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix121": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix122": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix123": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix124": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix125": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix126": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix127": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix128": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix129": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix130": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix131": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix132": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix133": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix134": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix135": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix136": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix137": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix138": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix139": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix140": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix141": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix142": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix143": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix144": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix145": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix146": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix147": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix148": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix149": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix150": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix151": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix152": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix153": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix154": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix155": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix156": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix157": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix158": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix159": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix160": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix161": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix162": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix163": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix164": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix165": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix166": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix167": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix168": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix169": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix170": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix171": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix172": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix173": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix174": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix175": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix176": {
          "kind": "struct",
          "resource": true
        },
        "zx/WaitItem": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix177": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix178": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix179": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix180": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix181": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix182": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix183": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix184": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix185": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix186": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix187": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix188": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix189": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix190": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix191": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix192": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix193": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix194": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix195": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix196": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix197": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix198": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix199": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix200": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix201": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix202": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix203": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix204": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix205": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix206": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix207": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix208": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix209": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix210": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix211": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix212": {
          "kind": "struct",
          "resource": true
        },
        "zx/PciBar": {
          "kind": "struct",
          "resource": false
        },
        "zx/PcieDeviceInfo": {
          "kind": "struct",
          "resource": false
        },
        "zx/PciInitArg": {
          "kind": "struct",
          "resource": false
        },
        "zx/SomeLongAnonymousPrefix213": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix214": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix215": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix216": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix217": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix218": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix219": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix220": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix221": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix222": {
          "kind": "struct",
          "resource": true
        },
        "zx/PacketSignal": {
          "kind": "struct",
          "resource": false
        },
        "zx/PacketException": {
          "kind": "struct",
          "resource": false
        },
        "zx/PacketGuestBell": {
          "kind": "struct",
          "resource": false
        },
        "zx/PacketGuestMem": {
          "kind": "struct",
          "resource": false
        },
        "zx/PacketGuestIo": {
          "kind": "struct",
          "resource": false
        },
        "zx/PacketGuestVcpu": {
          "kind": "struct",
          "resource": false
        },
        "zx/PacketInterrupt": {
          "kind": "struct",
          "resource": false
        },
        "zx/PacketPageRequest": {
          "kind": "struct",
          "resource": false
        },
        "zx/PortPacket": {
          "kind": "struct",
          "resource": false
        },
        "zx/SomeLongAnonymousPrefix223": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix224": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix225": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix226": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix227": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix228": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix229": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix230": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix231": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix232": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix233": {
          "kind": "struct",
          "resource": true
        },
        "zx/ProfileInfo": {
          "kind": "struct",
          "resource": false
        },
        "zx/SomeLongAnonymousPrefix234": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix235": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix236": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix237": {
          "kind": "struct",
          "resource": true
        },
        "zx/SmcParameters": {
          "kind": "struct",
          "resource": false
        },
        "zx/SmcResult": {
          "kind": "struct",
          "resource": false
        },
        "zx/SomeLongAnonymousPrefix238": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix239": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix240": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix241": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix242": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix243": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix244": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix245": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix246": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix247": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix248": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix249": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix250": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix251": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix252": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix253": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix254": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix255": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix256": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix257": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix258": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix259": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix260": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix261": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix262": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix263": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix264": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix265": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix266": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix267": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix268": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix269": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix270": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix271": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix272": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix273": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix274": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix275": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix276": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix277": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix278": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix279": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix280": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix281": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix282": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix283": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix284": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix285": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix286": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix287": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix288": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix289": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix290": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix291": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix292": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix293": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix294": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix295": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix296": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix297": {
          "kind": "struct",
          "resource": true
        },
        "zx/SystemPowerctlArg": {
          "kind": "struct",
          "resource": false
        },
        "zx/SomeLongAnonymousPrefix298": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix299": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix300": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix301": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix302": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix303": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix304": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix305": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix306": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix307": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix308": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix309": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix310": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix311": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix312": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix313": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix314": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix315": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix316": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix317": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix318": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix319": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix320": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix321": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix322": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix323": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix324": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix325": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix326": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix327": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix328": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix329": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix330": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix331": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix332": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix333": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix334": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix335": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix336": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix337": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix338": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix339": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix340": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix341": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix342": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix343": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix344": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix345": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix346": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix347": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix348": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix349": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix350": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix351": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix352": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix353": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix354": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix355": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix356": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix357": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix358": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix359": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix360": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix361": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix362": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix363": {
          "kind": "struct",
          "resource": true
        },
        "zx/SomeLongAnonymousPrefix364": {
          "kind": "struct",
          "resource": true
        },
        "zx/PacketUser": {
          "kind": "union",
          "resource": false
        },
        "zx/ProfileScheduler": {
          "kind": "union",
          "resource": false
        },
        "zx/ProfileInfoData": {
          "kind": "union",
          "resource": false
        },
        "zx/charptr": {
          "kind": "type_alias"
        },
        "zx/const_futexptr": {
          "kind": "type_alias"
        },
        "zx/const_voidptr": {
          "kind": "type_alias"
        },
        "zx/mutable_string": {
          "kind": "type_alias"
        },
        "zx/mutable_uint32": {
          "kind": "type_alias"
        },
        "zx/mutable_usize": {
          "kind": "type_alias"
        },
        "zx/mutable_vector_HandleDisposition_u32size": {
          "kind": "type_alias"
        },
        "zx/mutable_vector_HandleInfo_u32size": {
          "kind": "type_alias"
        },
        "zx/mutable_ChannelCallEtcArgs": {
          "kind": "type_alias"
        },
        "zx/mutable_vector_WaitItem": {
          "kind": "type_alias"
        },
        "zx/mutable_vector_handle_u32size": {
          "kind": "type_alias"
        },
        "zx/mutable_vector_void": {
          "kind": "type_alias"
        },
        "zx/mutable_vector_void_u32size": {
          "kind": "type_alias"
        },
        "zx/optional_PciBar": {
          "kind": "type_alias"
        },
        "zx/optional_PortPacket": {
          "kind": "type_alias"
        },
        "zx/optional_koid": {
          "kind": "type_alias"
        },
        "zx/optional_signals": {
          "kind": "type_alias"
        },
        "zx/optional_time": {
          "kind": "type_alias"
        },
        "zx/optional_uint32": {
          "kind": "type_alias"
        },
        "zx/optional_usize": {
          "kind": "type_alias"
        },
        "zx/optional_off": {
          "kind": "type_alias"
        },
        "zx/vector_HandleInfo_u32size": {
          "kind": "type_alias"
        },
        "zx/vector_handle_u32size": {
          "kind": "type_alias"
        },
        "zx/vector_paddr": {
          "kind": "type_alias"
        },
        "zx/vector_void": {
          "kind": "type_alias"
        },
        "zx/vector_iovec": {
          "kind": "type_alias"
        },
        "zx/vector_void_u32size": {
          "kind": "type_alias"
        },
        "zx/voidptr": {
          "kind": "type_alias"
        },
        "zx/string_view": {
          "kind": "type_alias"
        },
        "zx/HandleOp": {
          "kind": "type_alias"
        },
        "zx/Futex": {
          "kind": "type_alias"
        },
        "zx/VmOption": {
          "kind": "type_alias"
        },
        "zx/status": {
          "kind": "type_alias"
        },
        "zx/time": {
          "kind": "type_alias"
        },
        "zx/duration": {
          "kind": "type_alias"
        },
        "zx/ticks": {
          "kind": "type_alias"
        },
        "zx/koid": {
          "kind": "type_alias"
        },
        "zx/vaddr": {
          "kind": "type_alias"
        },
        "zx/paddr": {
          "kind": "type_alias"
        },
        "zx/paddr32": {
          "kind": "type_alias"
        },
        "zx/gpaddr": {
          "kind": "type_alias"
        },
        "zx/off": {
          "kind": "type_alias"
        },
        "zx/procarg": {
          "kind": "type_alias"
        },
        "zx/signals": {
          "kind": "type_alias"
        },
        "zx/usize": {
          "kind": "type_alias"
        },
        "zx/uintptr": {
          "kind": "type_alias"
        }
      }
    }
  ],
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "experimental_resource_declarations": [],
  "interface_declarations": [
    {
      "name": "fidl.examples.echo/Echo",
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 6,
        "column": 10,
        "length": 4
      },
      "maybe_attributes": [
        {
          "name": "Discoverable",
          "value": ""
        }
      ],
      "methods": [
        {
          "ordinal": 2936880781197466513,
          "name": "EchoString",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 7,
            "column": 3,
            "length": 10
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "string",
                "nullable": true
              },
              "name": "value",
              "location": {
                "filename": "fidlbolt.fidl",
                "line": 7,
                "column": 22,
                "length": 5
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 0
              }
            }
          ],
          "maybe_request_payload": "fidl.examples.echo/SomeLongAnonymousPrefix0",
          "maybe_request_type_shape_v1": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false,
            "is_resource": false
          },
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "string",
                "nullable": true
              },
              "name": "response",
              "location": {
                "filename": "fidlbolt.fidl",
                "line": 7,
                "column": 41,
                "length": 8
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 0
              }
            }
          ],
          "maybe_response_payload": "fidl.examples.echo/SomeLongAnonymousPrefix1",
          "maybe_response_type_shape_v1": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false,
            "is_resource": false
          },
          "is_composed": false
        },
        {
          "ordinal": 9059114273465311787,
          "name": "EchoHandle",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 8,
            "column": 3,
            "length": 10
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "handle",
                "subtype": "handle",
                "rights": 2147483648,
                "nullable": false
              },
              "name": "handle",
              "location": {
                "filename": "fidlbolt.fidl",
                "line": 8,
                "column": 24,
                "length": 6
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 4
              }
            }
          ],
          "maybe_request_payload": "fidl.examples.echo/SomeLongAnonymousPrefix2",
          "maybe_request_type_shape_v1": {
            "inline_size": 24,
            "alignment": 8,
            "depth": 0,
            "max_handles": 1,
            "max_out_of_line": 0,
            "has_padding": true,
            "has_flexible_envelope": false,
            "is_resource": true
          },
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "handle",
                "subtype": "handle",
                "rights": 2147483648,
                "nullable": false
              },
              "name": "handle",
              "location": {
                "filename": "fidlbolt.fidl",
                "line": 8,
                "column": 46,
                "length": 6
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 4
              }
            }
          ],
          "maybe_response_payload": "fidl.examples.echo/SomeLongAnonymousPrefix3",
          "maybe_response_type_shape_v1": {
            "inline_size": 24,
            "alignment": 8,
            "depth": 0,
            "max_handles": 1,
            "max_out_of_line": 0,
            "has_padding": true,
            "has_flexible_envelope": false,
            "is_resource": true
          },
          "is_composed": false
        },
        {
          "ordinal": 1120886698987607603,
          "name": "OnPong",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 9,
            "column": 6,
            "length": 6
          },
          "has_request": false,
          "has_response": true,
          "maybe_response": [],
          "maybe_response_type_shape_v1": {
            "inline_size": 16,
            "alignment": 8,
            "depth": 0,
            "max_handles": 0,
            "max_out_of_line": 0,
            "has_padding": false,
            "has_flexible_envelope": false,
            "is_resource": false
          },
          "is_composed": false
        }
      ]
    }
  ],
  "service_declarations": [],
  "struct_declarations": [
    {
      "name": "fidl.examples.echo/SomeLongAnonymousPrefix0",
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 7,
        "column": 13,
        "length": 15
      },
      "anonymous": true,
      "members": [
        {
          "type": {
            "kind": "string",
            "nullable": true
          },
          "name": "value",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 7,
            "column": 22,
            "length": 5
          },
          "field_shape_v1": {
            "offset": 0,
            "padding": 0
          }
        }
      ],
      "resource": true,
      "type_shape_v1": {
        "inline_size": 16,
        "alignment": 8,
        "depth": 1,
        "max_handles": 0,
        "max_out_of_line": 4294967295,
        "has_padding": true,
        "has_flexible_envelope": false,
        "is_resource": false
      }
    },
    {
      "name": "fidl.examples.echo/SomeLongAnonymousPrefix1",
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 7,
        "column": 32,
        "length": 18
      },
      "anonymous": true,
      "members": [
        {
          "type": {
            "kind": "string",
            "nullable": true
          },
          "name": "response",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 7,
            "column": 41,
            "length": 8
          },
          "field_shape_v1": {
            "offset": 0,
            "padding": 0
          }
        }
      ],
      "resource": true,
      "type_shape_v1": {
        "inline_size": 16,
        "alignment": 8,
        "depth": 1,
        "max_handles": 0,
        "max_out_of_line": 4294967295,
        "has_padding": true,
        "has_flexible_envelope": false,
        "is_resource": false
      }
    },
    {
      "name": "fidl.examples.echo/SomeLongAnonymousPrefix2",
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 8,
        "column": 13,
        "length": 18
      },
      "anonymous": true,
      "members": [
        {
          "type": {
            "kind": "handle",
            "subtype": "handle",
            "rights": 2147483648,
            "nullable": false
          },
          "name": "handle",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 8,
            "column": 24,
            "length": 6
          },
          "field_shape_v1": {
            "offset": 0,
            "padding": 4
          }
        }
      ],
      "resource": true,
      "type_shape_v1": {
        "inline_size": 8,
        "alignment": 8,
        "depth": 0,
        "max_handles": 1,
        "max_out_of_line": 0,
        "has_padding": true,
        "has_flexible_envelope": false,
        "is_resource": true
      }
    },
    {
      "name": "fidl.examples.echo/SomeLongAnonymousPrefix3",
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 8,
        "column": 35,
        "length": 18
      },
      "anonymous": true,
      "members": [
        {
          "type": {
            "kind": "handle",
            "subtype": "handle",
            "rights": 2147483648,
            "nullable": false
          },
          "name": "handle",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 8,
            "column": 46,
            "length": 6
          },
          "field_shape_v1": {
            "offset": 0,
            "padding": 4
          }
        }
      ],
      "resource": true,
      "type_shape_v1": {
        "inline_size": 8,
        "alignment": 8,
        "depth": 0,
        "max_handles": 1,
        "max_out_of_line": 0,
        "has_padding": true,
        "has_flexible_envelope": false,
        "is_resource": true
      }
    }
  ],
  "table_declarations": [],
  "union_declarations": [],
  "type_alias_declarations": [],
  "declaration_order": [
    "fidl.examples.echo/Echo"
  ],
  "declarations": {
    "fidl.examples.echo/Echo": "interface",
    "fidl.examples.echo/SomeLongAnonymousPrefix0": "struct",
    "fidl.examples.echo/SomeLongAnonymousPrefix1": "struct",
    "fidl.examples.echo/SomeLongAnonymousPrefix2": "struct",
    "fidl.examples.echo/SomeLongAnonymousPrefix3": "struct",
    "fidl.examples.echo/SomeLongAnonymousPrefix4": "struct"
  }
}
)";

static fidl_codec::LibraryLoader* test_library_loader = nullptr;

fidl_codec::LibraryLoader* GetTestLibraryLoader() {
  if (test_library_loader == nullptr) {
    test_library_loader = new fidl_codec::LibraryLoader();
    fidl_codec::LibraryReadError err;
    test_library_loader->AddContent(echo_service, &err);
  }
  return test_library_loader;
}
