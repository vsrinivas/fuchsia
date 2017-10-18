// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/split.h"

#include <limits>
#include <sstream>

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/callback/waiter.h"
#include "peridot/bin/ledger/glue/crypto/hash.h"
#include "peridot/bin/ledger/storage/impl/constants.h"
#include "peridot/bin/ledger/storage/impl/file_index.h"
#include "peridot/bin/ledger/storage/impl/file_index_generated.h"
#include "peridot/bin/ledger/storage/impl/object_digest.h"
#include "peridot/bin/ledger/storage/public/data_source.h"
#include "peridot/bin/ledger/third_party/bup/bupsplit.h"

namespace storage {

namespace {
constexpr size_t kMinChunkSize = 4 * 1024;
constexpr size_t kMaxChunkSize = std::numeric_limits<uint16_t>::max();
constexpr size_t kBitsPerLevel = 4;
// The max number of indentifiers that an index can contain so that the file
// size is less than |kMaxChunkSize|.
constexpr size_t kMaxDigestsPerIndex = kMaxChunkSize / 61;

using ObjectDigestAndSize = FileIndexSerialization::ObjectDigestAndSize;

struct ChunkAndSize {
  std::unique_ptr<DataSource::DataChunk> chunk;
  uint64_t size;
};

// Handles the successive callbacks from the DataSource.
//
// Algorithm:
// This class keeps track of a list of identifiers per level. For each level,
// the list must be aggregated into an index file, or if alone at the highest
// level when the algorithm ends, sent to the client.
// The algorithm reads data from the source and feeds it to the rolling hash.
// For each chunk cut by the rolling hash, the identifier of the chunk is added
// at level 0. The rolling hash algorithm also returns the number of index files
// that need to be built. An index file is also built as soon as a level
// contains |kMaxDigestsPerIndex| digests.
// When the algorithm builds the index at level |n| it does the following:
// For all levels from 0 to |n|:
//   - Build the index file at the given level. As a special case, if there is
//     a single object at the given level, just move it to the next level and
//     continue.
//   - Send the index file to the client.
//   - Add the identifier of the index file at the next level.
class SplitContext {
 public:
  explicit SplitContext(
      std::function<void(IterationStatus,
                         ObjectDigest,
                         std::unique_ptr<DataSource::DataChunk>)> callback)
      : callback_(std::move(callback)),
        roll_sum_split_(kMinChunkSize, kMaxChunkSize) {}
  SplitContext(SplitContext&& other) = default;
  ~SplitContext() {}

  void AddChunk(std::unique_ptr<DataSource::DataChunk> chunk,
                DataSource::Status status) {
    if (status == DataSource::Status::ERROR) {
      callback_(IterationStatus::ERROR, "", nullptr);
      return;
    }

    FXL_DCHECK(chunk || status == DataSource::Status::DONE);

    if (chunk) {
      ProcessChunk(std::move(chunk));
    }

    if (status != DataSource::Status::DONE) {
      return;
    }

    if (!current_chunks_.empty()) {
      // The remaining data needs to be sent even if it is not chunked at an
      // expected cut point.
      BuildAndSendNextChunk(views_.back().size());
    }

    // No data remains.
    FXL_DCHECK(current_chunks_.empty());

    // The final id to send exists.
    FXL_DCHECK(!current_digests_per_level_.back().empty());

    // This traverses the stack of indices, sending each level until a single
    // top level index is produced.
    for (size_t i = 0; i < current_digests_per_level_.size(); ++i) {
      if (current_digests_per_level_[i].empty()) {
        continue;
      }

      // At the top of the stack with a single element, the algorithm is
      // finished. The top-level object_digest is the unique element.
      if (i == current_digests_per_level_.size() - 1 &&
          current_digests_per_level_[i].size() == 1) {
        callback_(IterationStatus::DONE,
                  std::move(current_digests_per_level_[i][0].digest), nullptr);
        return;
      }

      BuildIndexAtLevel(i);
    }

    FXL_NOTREACHED();
  }

 private:
  std::vector<ObjectDigestAndSize>& GetCurrentDigestAtLevel(size_t level) {
    if (level >= current_digests_per_level_.size()) {
      FXL_DCHECK(level == current_digests_per_level_.size());
      current_digests_per_level_.resize(level + 1);
    }
    return current_digests_per_level_[level];
  }

  // Appends the given chunk to the unprocessed data and processes as much data
  // as possible using the rolling hash to determine where to cut the stream in
  // pieces.
  void ProcessChunk(std::unique_ptr<DataSource::DataChunk> chunk) {
    views_.push_back(chunk->Get());
    current_chunks_.push_back(std::move(chunk));

    while (!views_.empty()) {
      size_t bits;
      size_t split_index = roll_sum_split_.Feed(views_.back(), &bits);

      if (split_index == 0) {
        return;
      }

      BuildAndSendNextChunk(split_index);

      size_t level = GetLevel(bits);
      for (size_t i = 0; i < level; ++i) {
        FXL_DCHECK(!current_digests_per_level_[i].empty());
        BuildIndexAtLevel(i);
      }
    }
  }

  void BuildAndSendNextChunk(size_t split_index) {
    std::unique_ptr<DataSource::DataChunk> data = BuildNextChunk(split_index);
    auto data_view = data->Get();
    size_t size = data_view.size();
    ObjectDigest object_digest =
        ComputeObjectDigest(ObjectType::VALUE, data_view);
    callback_(IterationStatus::IN_PROGRESS, object_digest, std::move(data));
    AddIdentifierAtLevel(0, {std::move(object_digest), size});
  }

  void AddIdentifierAtLevel(size_t level, ObjectDigestAndSize data) {
    GetCurrentDigestAtLevel(level).push_back(std::move(data));

    if (current_digests_per_level_[level].size() < kMaxDigestsPerIndex) {
      // The level is not full, more identifiers can be added.
      return;
    }

    FXL_DCHECK(current_digests_per_level_[level].size() == kMaxDigestsPerIndex);
    // The level contains the max number of identifiers. Creating the index
    // file.

    AddIdentifierAtLevel(
        level + 1,
        BuildAndSendIndex(std::move(current_digests_per_level_[level])));
    current_digests_per_level_[level].clear();
  }

  void BuildIndexAtLevel(size_t level) {
    auto objects = std::move(current_digests_per_level_[level]);
    current_digests_per_level_[level].clear();

    if (objects.size() == 1) {
      AddIdentifierAtLevel(level + 1, std::move(objects.front()));
    } else {
      auto id_and_size = BuildAndSendIndex(std::move(objects));
      AddIdentifierAtLevel(level + 1, std::move(id_and_size));
    }
  }

  ObjectDigestAndSize BuildAndSendIndex(
      std::vector<ObjectDigestAndSize> digests_and_sizes) {
    FXL_DCHECK(digests_and_sizes.size() > 1);
    FXL_DCHECK(digests_and_sizes.size() <= kMaxDigestsPerIndex);

    std::unique_ptr<DataSource::DataChunk> chunk;
    size_t total_size;
    FileIndexSerialization::BuildFileIndex(digests_and_sizes, &chunk,
                                           &total_size);

    FXL_DCHECK(chunk->Get().size() <= kMaxChunkSize) << chunk->Get().size();
    ObjectDigest object_digest =
        ComputeObjectDigest(ObjectType::INDEX, chunk->Get());
    callback_(IterationStatus::IN_PROGRESS, object_digest, std::move(chunk));
    return {std::move(object_digest), total_size};
  }

  static size_t GetLevel(size_t bits) {
    FXL_DCHECK(bits >= bup::kBlobBits);
    return (bits - bup::kBlobBits) / kBitsPerLevel;
  }

  std::unique_ptr<DataSource::DataChunk> BuildNextChunk(size_t index) {
    FXL_DCHECK(current_chunks_.size() == views_.size());
    FXL_DCHECK(!current_chunks_.empty());
    FXL_DCHECK(views_.back().size() >= index);

    if (views_.size() == 1 && views_.front().size() == index &&
        views_.front().size() == current_chunks_.front()->Get().size()) {
      std::unique_ptr<DataSource::DataChunk> result =
          std::move(current_chunks_.front());
      views_.clear();
      current_chunks_.clear();
      return result;
    }

    std::string data;
    size_t total_size = index;

    for (size_t i = 0; i + 1 < views_.size(); ++i) {
      total_size += views_[i].size();
    }
    data.reserve(total_size);
    for (size_t i = 0; i + 1 < views_.size(); ++i) {
      data.append(views_[i].data(), views_[i].size());
    }

    fxl::StringView last = views_.back();
    data.append(last.data(), index);

    if (index < last.size()) {
      views_.clear();
      if (current_chunks_.size() > 1) {
        std::swap(current_chunks_.front(), current_chunks_.back());
        current_chunks_.resize(1);
      }
      views_.push_back(last.substr(index));
    } else {
      current_chunks_.clear();
      views_.clear();
    }

    FXL_DCHECK(current_chunks_.size() == views_.size());
    return DataSource::DataChunk::Create(std::move(data));
  }

  std::function<void(IterationStatus,
                     ObjectDigest,
                     std::unique_ptr<DataSource::DataChunk>)>
      callback_;
  bup::RollSumSplit roll_sum_split_;
  // The list of chunks from the initial source that are not yet entiretly
  // consumed.
  std::vector<std::unique_ptr<DataSource::DataChunk>> current_chunks_;
  // The list of data that has not yet been consumed. For all indexes, the view
  // at the given index is a view to the chunk at the same index.
  std::vector<fxl::StringView> views_;
  // List of unsent indices per level.
  std::vector<std::vector<ObjectDigestAndSize>> current_digests_per_level_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SplitContext);
};

class CollectPiecesState
    : public fxl::RefCountedThreadSafe<CollectPiecesState> {
 public:
  std::function<void(ObjectDigestView,
                     std::function<void(Status, fxl::StringView)>)>
      data_accessor;
  std::function<bool(IterationStatus, ObjectDigestView)> callback;
  bool running = true;
};

void CollectPiecesInternal(ObjectDigestView root,
                           fxl::RefPtr<CollectPiecesState> state,
                           fxl::Closure on_done) {
  if (!state->callback(IterationStatus::IN_PROGRESS, root)) {
    on_done();
    return;
  }

  if (GetObjectDigestType(root) != ObjectDigestType::INDEX_HASH) {
    on_done();
    return;
  }

  state->data_accessor(root, [state, on_done = std::move(on_done)](
                                 Status status, fxl::StringView data) mutable {
    if (!state->running) {
      on_done();
      return;
    }

    if (status != Status::OK) {
      FXL_LOG(WARNING) << "Unable to read object content.";
      state->running = false;
      on_done();
      return;
    }

    auto waiter = callback::CompletionWaiter::Create();
    status = ForEachPiece(data, [&](ObjectDigestView digest) {
      CollectPiecesInternal(digest, state, waiter->NewCallback());
      return Status::OK;
    });
    if (status != Status::OK) {
      state->running = false;
      on_done();
      return;
    }

    waiter->Finalize(std::move(on_done));
  });
}

}  // namespace

void SplitDataSource(
    DataSource* source,
    std::function<void(IterationStatus,
                       ObjectDigest,
                       std::unique_ptr<DataSource::DataChunk>)> callback) {
  SplitContext context(std::move(callback));
  source->Get(
      fxl::MakeCopyable([context = std::move(context)](
                            std::unique_ptr<DataSource::DataChunk> chunk,
                            DataSource::Status status) mutable {
        context.AddChunk(std::move(chunk), status);
      }));
}

Status ForEachPiece(fxl::StringView index_content,
                    std::function<Status(ObjectDigestView)> callback) {
  const FileIndex* file_index;
  Status status =
      FileIndexSerialization::ParseFileIndex(index_content, &file_index);
  if (status != Status::OK) {
    return status;
  }

  for (const auto* child : *file_index->children()) {
    Status status = callback(child->object_digest());
    if (status != Status::OK) {
      return status;
    }
  }

  return Status::OK;
}

void CollectPieces(
    ObjectDigestView root,
    std::function<void(ObjectDigestView,
                       std::function<void(Status, fxl::StringView)>)>
        data_accessor,
    std::function<bool(IterationStatus, ObjectDigestView)> callback) {
  auto state = fxl::AdoptRef(new CollectPiecesState());
  state->data_accessor = std::move(data_accessor);
  state->callback = std::move(callback);

  CollectPiecesInternal(root, state, [state] {
    IterationStatus final_status =
        state->running ? IterationStatus::DONE : IterationStatus::ERROR;
    state->callback(final_status, "");
  });
}

}  // namespace storage
