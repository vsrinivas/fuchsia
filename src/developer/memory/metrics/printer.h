// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_METRICS_PRINTER_H_
#define SRC_DEVELOPER_MEMORY_METRICS_PRINTER_H_

#include <iostream>
#include <sstream>
#include <string>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/digest.h"
#include "src/developer/memory/metrics/summary.h"
#include "src/lib/fxl/macros.h"

namespace memory {

extern const size_t kMaxFormattedStringSize;
const char* FormatSize(uint64_t size, char* buffer);

enum Sorted { UNSORTED, SORTED };

class Printer {
 public:
  explicit Printer(std::ostream& os) : os_(os) {}
  /* Outputs capture as JSON. In this format:
     {
       "Time":28477260758625,
       "Kernel":{
          "total":1610612736,
          "free":170565632,
          "wired":67395584,
          "total_heap":30904320,
          "free_heap":1873728,
          "vmo":1276194816,
          "mmu":65294336,
          "ipc":196608,
          "other":61440
        },
        "Processes":[
          ["koid","name","vmos"],
          [47325,"fonts.cm",[47353, ...]],
          ...
        ],
        "VmoNames":["scudo:primary", ...],
        "Vmos":[
          ["koid","name","parent_koid","committed_bytes","allocated_bytes"],
          [47440,38,47437,4096,4096],
          ...
        ]
      }
      For size reasons:
      - Processes and Vmos have an initial entry that contains the names of the the fields in the
        rest of the entries.
      - The names of the VMOs are an index into the VMONames array.
  */
  void PrintCapture(const Capture& capture);
  // `bucket_config` must be a valid JSON array, in this format:
  //      [
  //         {
  //            "event_code": 29,
  //            "name": "BlobfsInactive",
  //            "process": "blobfs\\.cm",
  //            "vmo": "inactive-blob-.*"
  //         },
  //         ...
  //      ]
  //
  //
  //  Outputs JSON in this format:
  // {"Capture":
  //      {
  //        "Time":28477260758625,
  //        "Kernel":{
  //           "total":1610612736,
  //           "free":170565632,
  //           "wired":67395584,
  //           "total_heap":30904320,
  //           "free_heap":1873728,
  //           "vmo":1276194816,
  //           "mmu":65294336,
  //           "ipc":196608,
  //           "other":61440
  //         },
  //         "Processes":[
  //           ["koid","name","vmos"],
  //           [47325,"fonts.cm",[47353, ...]],
  //           ...
  //         ],
  //         "VmoNames":["scudo:primary", ...],
  //         "Vmos":[
  //           ["koid","name","parent_koid","committed_bytes","allocated_bytes"],
  //           [47440,38,47437,4096,4096],
  //           ...
  //         ]
  //      }
  //   "Buckets":
  //      [
  //         {
  //            "event_code": 29,
  //            "name": "BlobfsInactive",
  //            "process": "blobfs\\.cm",
  //            "vmo": "inactive-blob-.*"
  //         },
  //         ...
  //      ]
  // }
  //
  //  For size reasons:
  //    - Processes and Vmos have an initial entry that contains the names of the the fields in the
  //      rest of the entries.
  //    - The names of the VMOs are an index into the VMONames array.
  void PrintCaptureAndBucketConfig(const Capture& capture, const std::string& bucket_config);
  void PrintSummary(const Summary& summary, CaptureLevel level, Sorted sorted);
  void PrintDigest(const Digest& digest);
  void OutputSummary(const Summary& summary, Sorted sorted, zx_koid_t pid);
  void OutputDigest(const Digest& digest);

 private:
  void OutputSizes(const Sizes& sizes);
  std::ostream& os_;
  FXL_DISALLOW_COPY_AND_ASSIGN(Printer);
};

}  // namespace memory

#endif  // SRC_DEVELOPER_MEMORY_METRICS_PRINTER_H_
