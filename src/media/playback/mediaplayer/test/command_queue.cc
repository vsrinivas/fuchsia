// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/command_queue.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include <iostream>

#include "lib/fidl/cpp/optional.h"
#include "lib/media/cpp/type_converters.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/url/gurl.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"

namespace media_player {
namespace test {

CommandQueue::CommandQueue() : dispatcher_(async_get_default_dispatcher()) {
  wait_for_position_task_.set_handler([this]() {
    if (wait_for_position_ != fuchsia::media::NO_TIMESTAMP) {
      wait_for_position_ = fuchsia::media::NO_TIMESTAMP;

      if (verbose_) {
        std::cerr << "WaitForPosition done\n";
      }

      ExecuteNextCommand();
    }
  });
}

CommandQueue::~CommandQueue() {}

void CommandQueue::NotifyStatusChanged(const fuchsia::media::playback::PlayerStatus& status) {
  status_ = fidl::MakeOptional(fidl::Clone(status));

  MaybeFinishWaitingForStatusCondition();

  if (status.timeline_function) {
    timeline_function_ = fidl::To<media::TimelineFunction>(*status.timeline_function);
    MaybeScheduleWaitForPositionTask();
    MaybeFinishWaitingForSeekCompletion();
  }
}

void CommandQueue::NotifyViewReady() {
  view_ready_ = true;
  MaybeFinishWaitingForViewReady();
}

void CommandQueue::MaybeFinishWaitingForViewReady() {
  if (view_ready_ && wait_for_view_ready_) {
    wait_for_view_ready_ = false;

    if (verbose_) {
      std::cerr << "WaitForViewReady done\n";
    }

    ExecuteNextCommand();
  }
}

void CommandQueue::MaybeFinishWaitingForStatusCondition() {
  if (status_ && wait_for_status_condition_ && wait_for_status_condition_(*status_)) {
    // We have status from the player, are waiting for a condition relating to
    // status to become true and have detected that, indeed, that condition has
    // become true. Clear the condition and continue command execution.
    wait_for_status_condition_ = nullptr;
    ExecuteNextCommand();
  }
}

void CommandQueue::MaybeScheduleWaitForPositionTask() {
  if (wait_for_position_ != fuchsia::media::NO_TIMESTAMP) {
    wait_for_position_task_.Cancel();
    if (timeline_function_.invertible()) {
      // Apply the timeline function in reverse to find the CLOCK_MONOTONIC
      // time at which we should resume executing commands.
      int64_t wait_for_time = timeline_function_.ApplyInverse(wait_for_position_);
      wait_for_position_task_.PostForTime(dispatcher_, zx::time(wait_for_time));
    }
  }
}

void CommandQueue::MaybeFinishWaitingForSeekCompletion() {
  if (wait_for_seek_completion_position_ != fuchsia::media::NO_TIMESTAMP &&
      timeline_function_.subject_time() == wait_for_seek_completion_position_) {
    wait_for_seek_completion_position_ = fuchsia::media::NO_TIMESTAMP;

    if (verbose_) {
      std::cerr << "WaitForSeekCompletion done\n";
    }

    ExecuteNextCommand();
  }
}

void CommandQueue::ExecuteNextCommand() {
  if (command_queue_.empty()) {
    return;
  }

  async::PostTask(dispatcher_, [this]() {
    if (command_queue_.empty()) {
      return;
    }

    auto command = std::move(command_queue_.front());
    command_queue_.pop();
    command->Execute(this);
  });
}

void CommandQueue::SetUrlCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "SetUrl " << url_ << "\n";
  }

  url::GURL url = url::GURL(url_);

  auto fd = fbl::unique_fd(open(url.path().c_str(), O_RDONLY));
  FX_CHECK(fd.is_valid());
  command_queue->player_->SetFileSource(fsl::CloneChannelFromFileDescriptor(fd.get()));
  command_queue->prev_seek_position_ = 0;
  command_queue->status_ = nullptr;
  command_queue->ExecuteNextCommand();
}

void CommandQueue::SetFileCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "SetFile\n";
  }

  auto fd = fbl::unique_fd(open(path_.c_str(), O_RDONLY));
  FX_CHECK(fd.is_valid());
  command_queue->player_->SetFileSource(fsl::CloneChannelFromFileDescriptor(fd.get()));
  command_queue->prev_seek_position_ = 0;
  command_queue->status_ = nullptr;
  command_queue->ExecuteNextCommand();
}

void CommandQueue::PlayCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "Play\n";
  }

  command_queue->player_->Play();
  command_queue->ExecuteNextCommand();
}

void CommandQueue::PauseCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "Pause\n";
  }

  command_queue->player_->Pause();
  command_queue->ExecuteNextCommand();
}

void CommandQueue::SeekCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "Seek " << AsNs(position_.get()) << "\n";
  }

  command_queue->player_->Seek(position_.get());
  command_queue->prev_seek_position_ = position_.get();
  command_queue->status_ = nullptr;
  command_queue->ExecuteNextCommand();
}

void CommandQueue::InvokeCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "Invoke\n";
  }

  FX_DCHECK(action_);
  action_();
  command_queue->ExecuteNextCommand();
}

void CommandQueue::WaitForStatusConditionCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "WaitForStatusConditionCommand\n";
  }

  command_queue->wait_for_status_condition_ = std::move(condition_);
  // |ExecuteNextCommand| will be called when |wait_for_status_condition_|
  // returns true.
  command_queue->MaybeFinishWaitingForStatusCondition();
}

void CommandQueue::WaitForViewReadyCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "WaitForViewReady\n";
  }

  command_queue->wait_for_view_ready_ = true;
  // |ExecuteNextCommand| will be called when the view is ready.
  command_queue->MaybeFinishWaitingForViewReady();
}

void CommandQueue::WaitForPositionCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "WaitForPosition " << AsNs(position_.get()) << "\n";
  }

  command_queue->wait_for_position_ = position_.get();
  // |ExecuteNextCommand| will be called when the position has been reached.
  command_queue->MaybeScheduleWaitForPositionTask();
}

void CommandQueue::WaitForSeekCompletionCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "WaitForSeekCompletion\n";
  }

  command_queue->wait_for_seek_completion_position_ = command_queue->prev_seek_position_;
  // |ExecuteNextCommand| will be called when the seek has completed.
  command_queue->MaybeFinishWaitingForSeekCompletion();
}

void CommandQueue::SleepCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "Sleep " << AsNs(duration_.get()) << "\n";
  }

  async::PostDelayedTask(
      command_queue->dispatcher_, [command_queue]() { command_queue->ExecuteNextCommand(); },
      zx::duration(duration_));
}

}  // namespace test
}  // namespace media_player
