// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_SOUNDS_SOUNDPLAYER_SOUND_H_
#define SRC_MEDIA_SOUNDS_SOUNDPLAYER_SOUND_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>

#include <fbl/unique_fd.h>

namespace soundplayer {

// Base class for |UndiscardableSound| and |DiscardableSound|, both containers for sound that
// wrap a VMO containing raw LPCM audio content.
class Sound {
 public:
  virtual ~Sound() = default;

  // Locks the sound VMO for reading, restoring its contents if it has been discarded. Locks may
  // be nested arbitrarily (with a mix of |LockForRead| and |LockForWrite|). Only the outermost
  // lock call has any effect aside from changing the nesting counter. There are no unexpected
  // side effects, but, of course, if the intent is to read valid data from the VMO, the VMO
  // should contain valid data.
  const zx::vmo& LockForRead();

  // Locks the sound VMO writing. If the VMO has been discarded, it is not restored. See the
  // comment for |LockForRead| above.
  const zx::vmo& LockForWrite();

  // Unlocks the sound after |LockForRead| or |LockForWrite|.
  void Unlock();

  // Size of the sound in the VMO.
  uint64_t size() const { return size_; }

  // Size of the entire VMO.
  uint64_t vmo_size() const { return vmo_size_; }

  const fuchsia::media::AudioStreamType& stream_type() const { return stream_type_; }

  zx::duration duration() const;

  uint64_t frame_count() const;

  uint32_t frame_size() const;

  uint32_t sample_size() const;

 protected:
  // Constructs a |Sound| from a non-discardable VMO.
  Sound(zx::vmo vmo, uint64_t size, fuchsia::media::AudioStreamType stream_type);

  Sound() = default;

  zx::vmo& vmo() { return vmo_; }

  void SetSize(size_t size, size_t vmo_size) {
    size_ = size;
    vmo_size_ = vmo_size;
  }

  void SetStreamType(fuchsia::media::AudioStreamType stream_type) { stream_type_ = stream_type; }

  // Applies a lock for reading on behalf of |LockForRead|, which handles the nesting counter.
  virtual void ApplyLockForRead() = 0;

  // Applies a lock for reading on behalf of |LockForWrite|, which handles the nesting counter.
  virtual void ApplyLockForWrite() = 0;

  // Removes a lock on behalf of |Unlock|, which handles the nesting counter.
  virtual void Removelock() = 0;

 private:
  zx::vmo vmo_;
  uint64_t size_ = 0;
  uint64_t vmo_size_ = 0;
  fuchsia::media::AudioStreamType stream_type_;
  int32_t lock_count_ = 0;
};

// Container for raw LPCM sound in a VMO. An |UndiscardableSound| is created from an existing
// non-discardable, non-resizeable, VMO. |LockForRead|, |LockForWrite| and |Unlock| do nothing.
class UndiscardableSound : public Sound {
 public:
  // Constructs a |Sound| from a non-discardable VMO.
  UndiscardableSound(zx::vmo vmo, uint64_t size, fuchsia::media::AudioStreamType stream_type)
      : Sound(std::move(vmo), size, stream_type) {}

 protected:
  void ApplyLockForRead() override {}

  void ApplyLockForWrite() override {}

  void Removelock() override {}
};

// Container for raw LPCM sound in a VMO. A |DiscardableSound| is created from a file descriptor.
// Full initialization requires |SetSize|, |SetStreamType| and |SetRestoreCallback| to be called.
class DiscardableSound : public Sound {
 public:
  explicit DiscardableSound(fbl::unique_fd fd) : fd_(std::move(fd)) {}

  DiscardableSound() = default;

  ~DiscardableSound() override = default;

  int fd() const { return fd_.get(); }

  // Sets the size and creates the VMO, or, if this method has already been called successfully,
  // verifies that the given size matches the size as it was previously set. In the former case,
  // an error is returned if the VMO creation fails. In the latter case, |ZX_ERR_INTERNAL| is
  // returned if the sizes don't match.
  //
  // This method must be called once before |LockForRead| or |LockForWrite| are called.
  zx_status_t SetSize(size_t size);

  // Sets the stream type, or, if this method has already be called successfully, verifies that the
  // given stream type matches the stream type as it was previously set. In the former case,
  // |ZX_OK| is always returned. In the later case, |ZX_ERR_INTERNAL| is returned if the stream
  // types don't match.
  //
  zx_status_t SetStreamType(fuchsia::media::AudioStreamType stream_type);

  // Sets the callback that restores the VMO when |LockForRead| is called and the VMO has been
  // discarded.
  //
  // If |LockForRead| is called before this method is called, no restoration will occur.
  void SetRestoreCallback(fit::closure callback) {
    FX_DCHECK(callback);
    restore_callback_ = std::move(callback);
  }

 protected:
  void ApplyLockForRead() override;

  void ApplyLockForWrite() override;

  void Removelock() override;

  // Restores the locked VMO using the restore callback. This is protected (rather than private) to
  // enable testing.
  void Restore();

 private:
  fbl::unique_fd fd_;
  fit::closure restore_callback_;
};

}  // namespace soundplayer

#endif  // SRC_MEDIA_SOUNDS_SOUNDPLAYER_SOUND_H_
