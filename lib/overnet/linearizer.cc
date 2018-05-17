// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linearizer.h"

namespace overnet {

Linearizer::Linearizer(uint64_t max_buffer) : max_buffer_(max_buffer) {}

void Linearizer::ValidateInternals() const {
#ifndef NDEBUG
  // If closed, nothing should be pending
  if (closed_) {
    assert(pending_push_.empty());
    assert(ready_.empty());
  }
  // No pending read callback if the next thing is ready.
  if ((!pending_push_.empty() && pending_push_.begin()->first == offset_)) {
    assert(ready_.empty());
  }
  // The first thing in the pending queue should be after our read bytes.
  if (!pending_push_.empty()) assert(pending_push_.begin()->first >= offset_);
  // There should be no overlap between chunks in the pending map.
  uint64_t seen_to = offset_;
  for (const auto& el : pending_push_) {
    assert(seen_to <= el.first);
    seen_to = el.first + el.second.first.length();
    assert(!el.second.second.empty());
  }
  // Should not exceed our buffering limits.
  if (!pending_push_.empty()) {
    auto last = std::prev(pending_push_.end());
    assert(last->first + last->second.first.length() <= offset_ + max_buffer_);
  }
#endif
}

void Linearizer::Close(const Status& status) {
  Close(status, Callback<void>::Ignored());
}

void Linearizer::Close(const Status& status, Callback<void> quiesced) {
  if (closed_) return;
  closed_ = true;
  if (!pending_push_.empty() && status.is_ok()) {
    closed_error_ = Status(StatusCode::CANCELLED, "Gaps existed at close time");
  } else {
    closed_error_ = status;
  }
  if (!ready_.empty()) {
    if (closed_error_.is_ok()) {
      ready_(Optional<Slice>());
    } else {
      ready_(closed_error_);
    }
  }
  for (auto& el : pending_push_) {
    el.second.second(closed_error_);
  }
  pending_push_.clear();
}

void Linearizer::Push(Chunk chunk, StatusCallback done) {
  ValidateInternals();

  uint64_t chunk_start = chunk.offset;
  const uint64_t chunk_end = chunk_start + chunk.slice.length();

  // Check whether the chunk is within our buffering limits
  // (if not we can reject and hope for a resend.)
  if (chunk_end > offset_ + max_buffer_) {
    done(Status(StatusCode::RESOURCE_EXHAUSTED,
                "Linearizer max buffer exceeded"));
    return;
  }

  if (length_) {
    if (chunk_end > *length_) {
      Close(Status(StatusCode::INVALID_ARGUMENT,
                   "Received chunk past end of message"));
    } else if (chunk.end_of_message && *length_ != chunk_end) {
      Close(Status(StatusCode::INVALID_ARGUMENT,
                   "Received ambiguous end of message point"));
    }
  } else if (chunk.end_of_message) {
    if (offset_ > chunk_end) {
      Close(Status(StatusCode::INVALID_ARGUMENT,
                   "Already read past end of message"));
    }
    if (!pending_push_.empty()) {
      const auto it = pending_push_.rbegin();
      const auto end = it->first + it->second.first.length();
      if (end > chunk_end) {
        Close(Status(StatusCode::INVALID_ARGUMENT,
                     "Already received bytes past end of message"));
      }
    }
    if (offset_ == chunk_end) {
      Close(Status::Ok());
    }
  }

  if (closed_) {
    done(closed_error_);
    return;
  }

  if (chunk.end_of_message && !length_) {
    length_ = chunk_end;
  }

  // Fast path: already a pending read ready, this chunk is at the head of what
  // we're waiting for, and overlaps with nothing.
  if (!ready_.empty() && chunk_start == offset_ &&
      (pending_push_.empty() || pending_push_.begin()->first > chunk_end)) {
    offset_ += chunk.slice.length();
    auto ready = std::move(ready_);
    if (length_) {
      assert(offset_ <= *length_);
      if (offset_ == *length_) {
        Close(Status::Ok());
      }
    }
    ready(std::move(chunk.slice));
    done(Status::Ok());
    return;
  }

  // If the chunk is partially before the start of what we've delivered, we can
  // trim.
  // If it's wholly before, then we can discard.
  if (chunk_start < offset_) {
    if (chunk_end > offset_) {
      chunk.TrimBegin(offset_ - chunk_start);
      chunk_start = chunk.offset;
    } else {
      done(Status::Ok());
      return;
    }
  }

  // Slow path: we first integrate this chunk into pending_push_, and then see
  // if we can trigger any completions.
  // We break out the integration into a separate function since it has many
  // exit conditions, and we've got some common checks to do once it's finished.
  if (pending_push_.empty()) {
    pending_push_.emplace(
        chunk.offset, std::make_pair(std::move(chunk.slice), std::move(done)));
  } else {
    IntegratePush(std::move(chunk), std::move(done));
  }

  if (!ready_.empty()) {
    Pull(std::move(ready_));
  }
}

void Linearizer::IntegratePush(Chunk chunk, StatusCallback done) {
  assert(!pending_push_.empty());

  auto lb = pending_push_.lower_bound(chunk.offset);
  if (lb != pending_push_.end() && lb->first == chunk.offset) {
    // Coincident with another chunk we've already received.
    // First check whether the common bytes are the same.
    const size_t common_length =
        std::min(chunk.slice.length(), lb->second.first.length());
    if (0 !=
        memcmp(chunk.slice.begin(), lb->second.first.begin(), common_length)) {
      Close(Status(StatusCode::DATA_LOSS,
                   "Linearizer received different bytes for the same span"));
      done(closed_error_);
    } else if (chunk.slice.length() <= lb->second.first.length()) {
      // New chunk is shorter than what's there (or the same length): We're
      // done.
      done(Status::Ok());
    } else {
      // New chunk is bigger than what's there: we create a new (tail) chunk and
      // continue integration
      chunk.TrimBegin(lb->second.first.length());
      IntegratePush(std::move(chunk), std::move(done));
    }
    // Early out.
    return;
  }

  if (lb != pending_push_.begin()) {
    // Find the chunk *before* this one
    const auto before = std::prev(lb);
    assert(before->first < chunk.offset);
    // Check to see if that chunk overlaps with this one.
    const size_t before_end = before->first + before->second.first.length();
    if (before_end > chunk.offset) {
      // Prior chunk overlaps with this one.
      // First check whether the common bytes are the same.
      const size_t common_length =
          std::min(before_end - chunk.offset, uint64_t(chunk.slice.length()));
      if (0 !=
          memcmp(before->second.first.begin() + (chunk.offset - before->first),
                 chunk.slice.begin(), common_length)) {
        Close(Status(StatusCode::DATA_LOSS,
                     "Linearizer received different bytes for the same span"));
        done(closed_error_);
      } else if (before_end >= chunk.offset + chunk.slice.length()) {
        // New chunk is a subset of the one before: we're done.
        done(Status::Ok());
      } else {
        // Trim the new chunk and continue integration.
        chunk.TrimBegin(before_end - chunk.offset);
        IntegratePush(std::move(chunk), std::move(done));
      }
      // Early out.
      return;
    }
  }

  if (lb != pending_push_.end()) {
    // Find the chunk *after* this one.
    const auto after = lb;
    assert(after->first > chunk.offset);
    // Check to see if that chunk overlaps with this one.
    if (after->first < chunk.offset + chunk.slice.length()) {
      const size_t common_length =
          std::min(chunk.offset + chunk.slice.length() - after->first,
                   uint64_t(after->second.first.length()));
      if (0 != memcmp(after->second.first.begin(),
                      chunk.slice.begin() + (after->first - chunk.offset),
                      common_length)) {
        Close(Status(StatusCode::DATA_LOSS,
                     "Linearizer received different bytes for the same span"));
        done(closed_error_);
        return;
      } else if (after->first + after->second.first.length() <
                 chunk.offset + chunk.slice.length()) {
        // Split chunk into two and integrate each separately
        struct DoneData {
          StatusCallback cb;
          int refs;
          Linearizer* owner;
          void Callback(const Status& status) {
            if (!cb.empty()) {
              if (status.is_error()) {
                cb(status);
              }
            }
            if (--refs == 0) {
              if (!cb.empty()) cb(Status::Ok());
              delete this;
            }
          }
        };
        DoneData* done_data = new DoneData{std::move(done), 2, this};
        Chunk tail = chunk;
        chunk.TrimEnd(chunk.offset + chunk.slice.length() - after->first);
        tail.TrimBegin(after->first + after->second.first.length() -
                       tail.offset);
        IntegratePush(std::move(chunk),
                      StatusCallback([done_data](Status&& status) {
                        done_data->Callback(std::forward<Status>(status));
                      }));
        IntegratePush(std::move(tail),
                      StatusCallback([done_data](Status&& status) {
                        done_data->Callback(std::forward<Status>(status));
                      }));
        return;
      } else {
        // Trim so the new chunk no longer overlaps.
        chunk.TrimEnd(chunk.offset + chunk.slice.length() - after->first);
      }
    }
  }

  // We now have a non-overlapping chunk that we can insert.
  pending_push_.emplace_hint(
      lb, chunk.offset,
      std::make_pair(std::move(chunk.slice), std::move(done)));
}

void Linearizer::Pull(StatusOrCallback<Optional<Slice>> ready) {
  ValidateInternals();
  if (closed_) {
    if (closed_error_.is_ok()) {
      ready(Optional<Slice>());
    } else {
      ready(closed_error_);
    }
    return;
  }
  // Check to see if there's data already available.
  auto it = pending_push_.begin();
  if (it != pending_push_.end() && it->first == offset_) {
    // There is! Send it up instantly.
    StatusCallback done = std::move(it->second.second);
    Slice slice = std::move(it->second.first);
    pending_push_.erase(it);
    offset_ += slice.length();
    if (length_) {
      assert(offset_ <= *length_);
      if (offset_ == *length_) {
        Close(Status::Ok());
      }
    }
    ready(std::move(slice));
    done(Status::Ok());
    return;
  }
  // There's not, signal that we can take some.
  // Note that this will cancel any pending Pull().
  ready_ = std::move(ready);
  ValidateInternals();
}

}  // namespace overnet
