// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/audio_server_impl.h"

#include "apps/media/lib/flog/flog.h"
#include "apps/media/src/audio_server/audio_output_manager.h"
#include "apps/media/src/audio_server/audio_renderer_impl.h"
#include "lib/mtl/tasks/message_loop.h"

namespace media {
namespace audio {

AudioServerImpl::AudioServerImpl()
    : application_context_(
          app::ApplicationContext::CreateFromStartupInfo()),
      output_manager_(this),
      cleanup_queue_(new CleanupQueue) {
  FTL_DCHECK(application_context_);

  FLOG_INITIALIZE(application_context_.get(), "audio_server");

  application_context_->outgoing_services()->AddService<AudioServer>(
      [this](fidl::InterfaceRequest<AudioServer> request) {
        bindings_.AddBinding(this, std::move(request));
      });

  // Stash a pointer to our task runner.
  FTL_DCHECK(mtl::MessageLoop::GetCurrent());
  task_runner_ = mtl::MessageLoop::GetCurrent()->task_runner();
  FTL_DCHECK(task_runner_);

  // Set up our output manager.
  MediaResult res = output_manager_.Init();
  // TODO(johngro): Do better at error handling than this weak check.
  FTL_DCHECK(res == MediaResult::OK);
}

AudioServerImpl::~AudioServerImpl() {
  Shutdown();
  FTL_DCHECK(cleanup_queue_);
  FTL_DCHECK(cleanup_queue_->empty());
}

void AudioServerImpl::Shutdown() {
  shutting_down_ = true;

  while (renderers_.size()) {
    // Renderers remove themselves from the server's set of active renderers as
    // they shutdown.  Assert that the set's size is shrinking by one each time
    // we shut down a renderer so we know that we are making progress.
    size_t size_before = renderers_.size();
    (*renderers_.begin())->Shutdown();
    size_t size_after = renderers_.size();
    FTL_DCHECK(size_after < size_before);
  }

  output_manager_.Shutdown();
  DoPacketCleanup();
}

void AudioServerImpl::CreateRenderer(
    fidl::InterfaceRequest<AudioRenderer> audio_renderer,
    fidl::InterfaceRequest<MediaRenderer> media_renderer) {
  renderers_.insert(AudioRendererImpl::Create(std::move(audio_renderer),
                                              std::move(media_renderer), this));
}

void AudioServerImpl::DoPacketCleanup() {
  // In order to minimize the time we spend in the lock, we allocate a new
  // queue, then lock, swap and clear the sched flag, and finally clean out the
  // queue (which has the side effect of triggering all of the send packet
  // callbacks).
  //
  // Note: this is only safe because we know that we are executing on a single
  // threaded task runner.  Without this guarantee, it might be possible call
  // the send packet callbacks for a media pipe in a different order than the
  // packets were sent in the first place.  If the task_runner for the audio
  // server ever loses this serialization guarantee (because it becomes
  // multi-threaded, for example) we will need to introduce another lock
  // (different from the cleanup lock) in order to keep the cleanup tasks
  // properly ordered while guaranteeing minimal contention of the cleanup lock
  // (which is being acquired by the high priority mixing threads).
  std::unique_ptr<CleanupQueue> tmp_queue(new CleanupQueue());

  {
    ftl::MutexLocker locker(&cleanup_queue_mutex_);
    cleanup_queue_.swap(tmp_queue);
    cleanup_scheduled_ = false;
  }

  // The clear method of standard containers do not guarantee any ordering of
  // destruction of the objects they hold.  In order to guarantee proper
  // sequencing of the callbacks, go over the container front-to-back, nulling
  // out the std::unique_ptrs they hold as we go (which will trigger the
  // callbacks).  Afterwards, just let tmp_queue go out of scope and clear()
  // itself automatically.
  for (auto iter = tmp_queue->begin(); iter != tmp_queue->end(); ++iter) {
    (*iter) = nullptr;
  }
}

void AudioServerImpl::SchedulePacketCleanup(
    std::unique_ptr<MediaPacketConsumerBase::SuppliedPacket> supplied_packet) {
  ftl::MutexLocker locker(&cleanup_queue_mutex_);

  cleanup_queue_->emplace_back(std::move(supplied_packet));

  if (!cleanup_scheduled_ && !shutting_down_) {
    FTL_DCHECK(task_runner_);
    task_runner_->PostTask([this]() { DoPacketCleanup(); });
    cleanup_scheduled_ = true;
  }
}

}  // namespace audio
}  // namespace media
