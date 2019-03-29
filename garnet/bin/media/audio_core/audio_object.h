// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_OBJECT_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_OBJECT_H_

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/fit/function.h>

#include "garnet/bin/media/audio_core/fwd_decls.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace media::audio {

// An audio object is the simple base class for 4 major types of audio objects
// in the mixer; Outputs, Inputs, AudioRenderers and AudioCapturers.  It ensures
// that each of these objects is intrusively ref-counted, and remembers its type
// so that it may be safely downcast from a generic audio object to something
// more specific.
class AudioObject : public fbl::RefCounted<AudioObject> {
 public:
  enum class Type {
    Output,
    Input,
    AudioRenderer,
    AudioCapturer,
  };

  static std::shared_ptr<AudioLink> LinkObjects(
      const fbl::RefPtr<AudioObject>& source,
      const fbl::RefPtr<AudioObject>& dest);
  static void RemoveLink(const AudioLinkPtr& link);

  void UnlinkSources();
  void UnlinkDestinations();
  void Unlink() {
    UnlinkSources();
    UnlinkDestinations();
  }

  // PreventNewLinks
  //
  // Clears the new_links_allowed flag from within the context of the
  // links_lock.  This ensures that no new links may be added to this object
  // anymore.  Calling PreventNewLinks is one of the first steps in the process
  // of shutting down an AudioObject.
  //
  // TODO(johngro) : Consider eliminating this; given the way that links are
  // created and destroyed, it is not clear if it is needed anymore.
  void PreventNewLinks() {
    fbl::AutoLock lock(&links_lock_);
    new_links_allowed_ = false;
  }

  Type type() const { return type_; }
  bool is_output() const { return type() == Type::Output; }
  bool is_input() const { return type() == Type::Input; }
  bool is_audio_renderer() const { return type() == Type::AudioRenderer; }
  bool is_audio_capturer() const { return type() == Type::AudioCapturer; }

 protected:
  friend class fbl::RefPtr<AudioObject>;
  explicit AudioObject(Type type) : type_(type) {}
  virtual ~AudioObject() {}

  // Initialize(Source|Dest)Link
  //
  // Called on the AudioCore's main message loop any time a source and a
  // destination are being linked via AudioObject::LinkObjects.  By default,
  // these hooks do nothing, but AudioObject subclasses may use them to set the
  // properties of a link (or reject the link) before the link gets added to the
  // source and destination link sets.
  //
  // For example, Sources like an AudioRenderer override InitializeDestLink in
  // order to set the source gain and to make a copy of their pending packet
  // queue. Destinations like an output override InitializeSourceLink in order
  // to choose and initialize an appropriate resampling filter.
  //
  // @return MediaResult::OK if initialization succeeded, or an appropriate
  // error code otherwise.
  virtual zx_status_t InitializeSourceLink(const AudioLinkPtr& link);
  virtual zx_status_t InitializeDestLink(const AudioLinkPtr& link);

  fbl::Mutex links_lock_;

  // The set of links which this audio device is acting as a source for (eg; the
  // destinations that this object is sending to).  The target of each of these
  // links must be a either an Output or a AudioCapturer.
  AudioLinkSet dest_links_ FXL_GUARDED_BY(links_lock_);

  // The set of links which this audio device is acting as a destination for
  // (eg; the sources that that the object is receiving from).  The target of
  // each of these links must be a either an Output or a AudioCapturer.
  //
  // TODO(johngro): Order this by priority.  Use a fbl::WAVLTree (or some other
  // form of ordered intrusive container) so that we can easily remove and
  // re-insert a link if/when priority changes.
  //
  // Right now, we have no priorities, so this is just a set of
  // AudioRenderer/output links.
  AudioLinkSet source_links_ FXL_GUARDED_BY(links_lock_);

  // The following iterator functions accept a function (see below) and call it
  // sequentially with each destination link as a parameter. As described below,
  // depending on which iterator is used, either every link is guaranteed to be
  // included, or iteration will terminate early as soon as a task returns true.
  //
  // This iterator approach reduces our ability to use static thread analysis
  // effectively, so use with care. ForEachDestLink and ForAnyDestLink each
  // obtain the links_lock_ and hold it while each LinkFunction or
  // LinkBoolFunction is invoked. For this reason,
  //    1) Callers into the ForEachSourceLink, ForEachDestLink or ForAnyDestLink
  //           functions must not already hold links_lock_; additionally,
  //    2) A LinkFunction or LinkBoolFunction must not
  //        a) attempt to obtain links_lock_ directly, nor
  //        b) acquire any lock marked as acquired_before(links_lock_), nor
  //        c) call any function which excludes links_lock_.
  //

  // The inline_functions below reserve stack space for up to four pointers.
  // This can be increased as needed (but should NOT be needed any time soon).
  //
  // LinkFunction has no return value and is used with ForEach[Source|Dest]Link.
  using LinkFunction =
      fit::inline_function<void(const std::shared_ptr<AudioLink>& link),
                           sizeof(void*) * 4>;
  // Same as LinkFunction, but returns bool for early termination. This
  // return val is used by ForAnyDestLink (or a future ForAllDestLinks).
  // Currently stack space for one ptr is provided (the one caller needs 0).
  using LinkBoolFunction =
      fit::inline_function<bool(const std::shared_ptr<AudioLink>& link),
                           sizeof(void*) * 1>;

  // Link Iterators - these functions iterate upon LinkPacketSource types only.
  //
  // Run this task on AudioLinks in source_links_. All links will be called.
  void ForEachSourceLink(const LinkFunction& source_task)
      FXL_LOCKS_EXCLUDED(links_lock_);

  // Run this task on every AudioLink in dest_links_. All links will be called.
  void ForEachDestLink(const LinkFunction& dest_task)
      FXL_LOCKS_EXCLUDED(links_lock_);

  // Run this task on each dest link. If any returns 'true', ForAnyDestLink
  // immediately returns 'true' without calling the remaining links. If none
  // returns 'true' or if link set is empty, ForAnyDestLink returns 'false'.
  bool ForAnyDestLink(const LinkBoolFunction& dest_task)
      FXL_LOCKS_EXCLUDED(links_lock_);

  // TODO(mpuryear): it might be a good idea to introduce an auto-lock like
  // object to fbl::, to behave like a lock token for situations like this. With
  // proper tweaks to fbl::Mutex, this could for static analysis purposes seem
  // to obtain and release a mutex without actually doing so. In debug builds,
  // it could also assert that the mutex was held at object construction time.

  // Pros: we regain much of the TA protection, if lambdas add one of these
  // objects "holding" the proper lock at the start of their executions.

  // Cons: essentially all these lambdas must capture "this", to tell the TA
  // which instance of links_lock was being held. This price would be paid in
  // all builds, regardless of whether code gets generated as a result.
  //

 private:
  void UnlinkCleanup(AudioLinkSet* links);

  const Type type_;
  bool new_links_allowed_ FXL_GUARDED_BY(links_lock_) = true;
};

}  // namespace media::audio

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_OBJECT_H_
