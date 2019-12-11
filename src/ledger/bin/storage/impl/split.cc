// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/split.h"

#include <lib/fit/function.h>

#include <limits>
#include <memory>
#include <sstream>

#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/storage/impl/constants.h"
#include "src/ledger/bin/storage/impl/file_index.h"
#include "src/ledger/bin/storage/impl/file_index_generated.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/third_party/bup/bupsplit.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

namespace {
constexpr size_t kMinChunkSize = 4 * 1024;
constexpr size_t kMaxChunkSize = std::numeric_limits<uint16_t>::max();
constexpr size_t kBitsPerLevel = 4;
// Empiric maximal size for an identifier in an index file. This should be the
// smallest possible number that allow the Split tests to pass.
constexpr size_t kMaxIdentifierSize = 77;
// The max number of identifiers that an index can contain so that the file size
// is less than |kMaxChunkSize|.
constexpr size_t kMaxIdentifiersPerIndex = kMaxChunkSize / kMaxIdentifierSize;

using ObjectIdentifierAndSize = FileIndexSerialization::ObjectIdentifierAndSize;

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
// contains |kMaxIdentifiersPerIndex| identifiers.
// When the algorithm builds the index at level |n| it does the following:
// For all levels from 0 to |n|:
//   - Build the index file at the given level. As a special case, if there is
//     a single object at the given level, just move it to the next level and
//     continue.
//   - Send the index file to the client.
//   - Add the identifier of the index file at the next level.
class SplitContext {
 public:
  explicit SplitContext(fit::function<ObjectIdentifier(ObjectDigest)> make_object_identifier,
                        fit::function<uint64_t(uint64_t)> chunk_permutation,
                        fit::function<void(IterationStatus, std::unique_ptr<Piece>)> callback,
                        ObjectType object_type)
      : make_object_identifier_(std::move(make_object_identifier)),
        callback_(std::move(callback)),
        object_type_(object_type),
        roll_sum_split_(kMinChunkSize, kMaxChunkSize, std::move(chunk_permutation)) {}
  SplitContext(SplitContext&& other) = default;
  SplitContext(const SplitContext&) = delete;
  SplitContext& operator=(const SplitContext&) = delete;
  SplitContext& operator=(SplitContext&& other) = default;
  ~SplitContext() = default;

  void AddChunk(std::unique_ptr<DataSource::DataChunk> chunk, DataSource::Status status) {
    if (status == DataSource::Status::ERROR) {
      callback_(IterationStatus::ERROR, nullptr);
      return;
    }

    LEDGER_DCHECK(chunk || status == DataSource::Status::DONE);

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
    LEDGER_DCHECK(current_chunks_.empty());

    // The final id to send exists.
    LEDGER_DCHECK(!current_identifiers_per_level_.back().empty());

    // This traverses the stack of indices, sending each level until a single
    // top level index is produced.
    for (size_t i = 0; i < current_identifiers_per_level_.size(); ++i) {
      if (current_identifiers_per_level_[i].empty()) {
        continue;
      }

      // At the top of the stack with a single element, the algorithm is
      // finished. The top-level object_identifier is the unique element.
      if (i == current_identifiers_per_level_.size() - 1 &&
          current_identifiers_per_level_[i].size() == 1) {
        // This identifier may be recomputed by SendDone, so this is not
        // necessarily the final value that we are going to send, but we check
        // that we last called |SendInProgress| on it for consistency.
        LEDGER_DCHECK(current_identifiers_per_level_[i][0].identifier == latest_piece_.identifier);
        SendDone();
        return;
      }

      BuildIndexAtLevel(i);
    }
    LEDGER_NOTREACHED();
  }

 private:
  // Information about a piece of data (chunk or index) to be sent.
  struct PendingPiece {
    ObjectIdentifier identifier;
    std::unique_ptr<DataSource::DataChunk> data;

    bool ready() { return data != nullptr; }
  };

  // Returns the object identifier for |data| of the given |type|, and invokes
  // |callback_| with IN_PROGRESS status. Actually defers sending the object
  // until the next call of this method, because the last object needs to be
  // treated differently in |SendDone|. |children| must contain the identifiers
  // of the children pieces if |type| is INDEX, and be empty otherwise.
  ObjectIdentifier SendInProgress(PieceType type, std::unique_ptr<DataSource::DataChunk> data) {
    if (latest_piece_.ready()) {
      callback_(IterationStatus::IN_PROGRESS,
                std::make_unique<DataChunkPiece>(std::move(latest_piece_.identifier),
                                                 std::move(latest_piece_.data)));
    }
    auto data_view = data->Get();
    // object_type for inner (IN_PROGRESS) pieces is always BLOB, regardless of
    // the overall |object_type_|. It may need to be TREE_NODE if this is the
    // very last piece (DONE), but we do not know it at this stage. We account
    // for this by recomputing the object digest in |SendDone|. It does not
    // matter if we return a wrong identifier here, because it will not be used
    // at all if we are at the root piece.
    ObjectDigest object_digest = ComputeObjectDigest(type, ObjectType::BLOB, data_view);
    latest_piece_.identifier = make_object_identifier_(std::move(object_digest));
    latest_piece_.data = std::move(data);
    return latest_piece_.identifier;
  }

  // Recomputes the object identifier for the last object to send: since it is
  // the root of the piece hierarchy, it needs to have the |tree_node| bit set
  // if we are splitting a TreeNode. Then sends this object identifier as DONE.
  void SendDone() {
    LEDGER_DCHECK(latest_piece_.ready());
    auto data_view = latest_piece_.data->Get();
    ObjectDigest object_digest = ComputeObjectDigest(
        GetObjectDigestInfo(latest_piece_.identifier.object_digest()).piece_type, object_type_,
        data_view);
    latest_piece_.identifier = make_object_identifier_(std::move(object_digest));
    callback_(IterationStatus::DONE,
              std::make_unique<DataChunkPiece>(std::move(latest_piece_.identifier),
                                               std::move(latest_piece_.data)));
  }

  std::vector<ObjectIdentifierAndSize>& GetCurrentIdentifiersAtLevel(size_t level) {
    if (level >= current_identifiers_per_level_.size()) {
      LEDGER_DCHECK(level == current_identifiers_per_level_.size());
      current_identifiers_per_level_.resize(level + 1);
    }
    return current_identifiers_per_level_[level];
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
        LEDGER_DCHECK(!current_identifiers_per_level_[i].empty());
        BuildIndexAtLevel(i);
      }
    }
  }

  void BuildAndSendNextChunk(size_t split_index) {
    auto data = BuildNextChunk(split_index);
    auto size = data->Get().size();
    auto identifier = SendInProgress(PieceType::CHUNK, std::move(data));
    AddIdentifierAtLevel(0, {std::move(identifier), size});
  }

  void AddIdentifierAtLevel(size_t level, ObjectIdentifierAndSize data) {
    GetCurrentIdentifiersAtLevel(level).push_back(std::move(data));

    if (current_identifiers_per_level_[level].size() < kMaxIdentifiersPerIndex) {
      // The level is not full, more identifiers can be added.
      return;
    }

    LEDGER_DCHECK(current_identifiers_per_level_[level].size() == kMaxIdentifiersPerIndex);
    // The level contains the max number of identifiers. Creating the index
    // file.

    AddIdentifierAtLevel(level + 1,
                         BuildAndSendIndex(std::move(current_identifiers_per_level_[level])));
    current_identifiers_per_level_[level].clear();
  }

  void BuildIndexAtLevel(size_t level) {
    auto objects = std::move(current_identifiers_per_level_[level]);
    current_identifiers_per_level_[level].clear();

    if (objects.size() == 1) {
      AddIdentifierAtLevel(level + 1, std::move(objects.front()));
    } else {
      auto id_and_size = BuildAndSendIndex(std::move(objects));
      AddIdentifierAtLevel(level + 1, std::move(id_and_size));
    }
  }

  ObjectIdentifierAndSize BuildAndSendIndex(
      std::vector<ObjectIdentifierAndSize> identifiers_and_sizes) {
    LEDGER_DCHECK(identifiers_and_sizes.size() > 1);
    LEDGER_DCHECK(identifiers_and_sizes.size() <= kMaxIdentifiersPerIndex);

    std::unique_ptr<DataSource::DataChunk> chunk;
    size_t total_size;
    FileIndexSerialization::BuildFileIndex(identifiers_and_sizes, &chunk, &total_size);

    LEDGER_DCHECK(chunk->Get().size() <= kMaxChunkSize)
        << "Expected maximum of: " << kMaxChunkSize << ", but got: " << chunk->Get().size();

    auto identifier = SendInProgress(PieceType::INDEX, std::move(chunk));
    return {std::move(identifier), total_size};
  }

  static size_t GetLevel(size_t bits) {
    LEDGER_DCHECK(bits >= bup::kBlobBits);
    return (bits - bup::kBlobBits) / kBitsPerLevel;
  }

  std::unique_ptr<DataSource::DataChunk> BuildNextChunk(size_t index) {
    LEDGER_DCHECK(current_chunks_.size() == views_.size());
    LEDGER_DCHECK(!current_chunks_.empty());
    LEDGER_DCHECK(views_.back().size() >= index);

    if (views_.size() == 1 && views_.front().size() == index &&
        views_.front().size() == current_chunks_.front()->Get().size()) {
      std::unique_ptr<DataSource::DataChunk> result = std::move(current_chunks_.front());
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

    absl::string_view last = views_.back();
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

    LEDGER_DCHECK(current_chunks_.size() == views_.size());
    return DataSource::DataChunk::Create(std::move(data));
  }

  fit::function<ObjectIdentifier(ObjectDigest)> make_object_identifier_;
  fit::function<void(IterationStatus, std::unique_ptr<Piece>)> callback_;
  // The object encoded by DataSource.
  ObjectType object_type_;
  bup::RollSumSplit roll_sum_split_;
  // The list of chunks from the initial source that are not yet entirely
  // consumed.
  std::vector<std::unique_ptr<DataSource::DataChunk>> current_chunks_;
  // The list of data that has not yet been consumed. For all indexes, the view
  // at the given index is a view to the chunk at the same index.
  std::vector<absl::string_view> views_;
  // List of unsent indices per level.
  std::vector<std::vector<ObjectIdentifierAndSize>> current_identifiers_per_level_;
  // The most recent piece that is entirely consumed but not yet sent to
  // |callback_|.
  PendingPiece latest_piece_;
};

class CollectPiecesState : public fxl::RefCountedThreadSafe<CollectPiecesState> {
 public:
  fit::function<void(ObjectIdentifier, fit::function<void(Status, absl::string_view)>)>
      data_accessor;
  fit::function<bool(IterationStatus, ObjectIdentifier)> callback;
  bool running = true;
};

void CollectPiecesInternal(ObjectIdentifier root, fxl::RefPtr<CollectPiecesState> state,
                           fit::closure on_done) {
  if (!state->callback(IterationStatus::IN_PROGRESS, root)) {
    on_done();
    return;
  }

  if (GetObjectDigestInfo(root.object_digest()).piece_type != PieceType::INDEX) {
    on_done();
    return;
  }

  state->data_accessor(root, [state, factory = root.factory(), on_done = std::move(on_done)](
                                 Status status, absl::string_view data) mutable {
    if (!state->running) {
      on_done();
      return;
    }

    if (status != Status::OK) {
      LEDGER_LOG(WARNING) << "Unable to read object content.";
      state->running = false;
      on_done();
      return;
    }

    auto waiter = fxl::MakeRefCounted<callback::CompletionWaiter>();
    status = ForEachIndexChild(data, factory, [&](ObjectIdentifier identifier) {
      CollectPiecesInternal(std::move(identifier), state, waiter->NewCallback());
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

void SplitDataSource(DataSource* source, ObjectType object_type,
                     fit::function<ObjectIdentifier(ObjectDigest)> make_object_identifier,
                     fit::function<uint64_t(uint64_t)> chunk_permutation,
                     fit::function<void(IterationStatus, std::unique_ptr<Piece>)> callback) {
  SplitContext context(std::move(make_object_identifier), std::move(chunk_permutation),
                       std::move(callback), object_type);
  source->Get([context = std::move(context)](std::unique_ptr<DataSource::DataChunk> chunk,
                                             DataSource::Status status) mutable {
    context.AddChunk(std::move(chunk), status);
  });
}

Status ForEachIndexChild(absl::string_view index_content, ObjectIdentifierFactory* factory,
                         fit::function<Status(ObjectIdentifier)> callback) {
  const FileIndex* file_index;
  RETURN_ON_ERROR(FileIndexSerialization::ParseFileIndex(index_content, &file_index));

  for (const auto* child : *file_index->children()) {
    RETURN_ON_ERROR(callback(ToObjectIdentifier(child->object_identifier(), factory)));
  }

  return Status::OK;
}

void CollectPieces(
    ObjectIdentifier root,
    fit::function<void(ObjectIdentifier, fit::function<void(Status, absl::string_view)>)>
        data_accessor,
    fit::function<bool(IterationStatus, ObjectIdentifier)> callback) {
  auto state = fxl::MakeRefCounted<CollectPiecesState>();
  state->data_accessor = std::move(data_accessor);
  state->callback = std::move(callback);

  CollectPiecesInternal(root, state, [state] {
    IterationStatus final_status = state->running ? IterationStatus::DONE : IterationStatus::ERROR;
    state->callback(final_status, {});
  });
}

}  // namespace storage
