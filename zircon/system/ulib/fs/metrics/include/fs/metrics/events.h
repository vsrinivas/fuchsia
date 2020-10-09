// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_METRICS_EVENTS_H_
#define FS_METRICS_EVENTS_H_

#include <cstdint>
#include <iterator>

#include <fbl/algorithm.h>

namespace fs_metrics {

// Collection of all events being recorded by local storage.
enum class Event : uint32_t {
  // Vnode Level operations.
  kClose = 2,
  kRead = 3,
  kWrite = 4,
  kAppend = 5,
  kTruncate = 6,
  kSetAttr = 7,
  kGetAttr = 8,
  kReadDir = 10,
  kSync = 9,
  kLookUp = 11,
  kCreate = 12,
  kLink = 1,
  kUnlink = 13,

  // Fs Manager Level operation.
  kDataCorruption = 14,

  // Distribution of compression formats. Only used by blobfs.
  kCompression = 16,

  // Journal events
  kJournalWriteData = 18,
  kJournalWriteMetadata = 19,
  kJournalTrimData = 20,
  kJournalSync = 21,
  kJournalScheduleTask = 22,
  kJournalWriterWriteData = 23,
  kJournalWriterWriteMetadata = 24,
  kJournalWriterTrimData = 25,
  kJournalWriterSync = 26,
  kJournalWriterWriteInfoBlock = 27,

  // This is an event used to identify last or invalid event.
  kInvalidEvent = 28,
};

enum class CorruptionSource { kUnknown = 0, kFvm = 1, kBlobfs = 2, kMinfs = 3 };

enum class CorruptionType { kUnknown = 0, kMetadata = 1, kData = 2 };

enum class CompressionSource { kUnknown = 0, kBlobfs = 1 };

enum class CompressionFormat {
  kUnknown = 0,
  kUncompressed = 1,
  kCompressedLZ4 = 2,
  kCompressedZSTD = 3,
  kCompressedZSTDSeekable = 4,
  kCompressedZSTDChunked = 5,
  kNumFormats = 6
};

// Collection of Vnode Events.
constexpr Event kVnodeEvents[] = {
    Event::kClose,   Event::kRead,    Event::kWrite,   Event::kAppend, Event::kTruncate,
    Event::kSetAttr, Event::kGetAttr, Event::kReadDir, Event::kSync,   Event::kLookUp,
    Event::kCreate,  Event::kLink,    Event::kUnlink,
};

// Number of different metric types recorded at Vnode level.
constexpr uint64_t kVnodeEventCount = std::size(kVnodeEvents);

// Collection of FsManager events.
constexpr Event kFsManagerEvents[] = {Event::kDataCorruption};

// Number of different metric types recorded at Fs Manager level.
constexpr uint64_t kFsManagerEventCount = std::size(kFsManagerEvents);

constexpr Event kJournalEvents[] = {Event::kJournalWriteData,
                                    Event::kJournalWriteMetadata,
                                    Event::kJournalTrimData,
                                    Event::kJournalSync,
                                    Event::kJournalScheduleTask,
                                    Event::kJournalWriterWriteData,
                                    Event::kJournalWriterWriteMetadata,
                                    Event::kJournalWriterTrimData,
                                    Event::kJournalWriterSync,
                                    Event::kJournalWriterWriteInfoBlock};

// Number of different metric types recorded at journal level.
constexpr uint64_t kJournalEventCount = std::size(kJournalEvents);

// Total number of events in the registry.
constexpr uint64_t kEventCount = kVnodeEventCount + kFsManagerEventCount + kJournalEventCount;

}  // namespace fs_metrics

#endif  // FS_METRICS_EVENTS_H_
