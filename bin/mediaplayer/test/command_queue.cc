// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/test/command_queue.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <iostream>
#include "garnet/bin/mediaplayer/graph/formatting.h"
#include "lib/fsl/io/fd.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/type_converters.h"
#include "lib/url/gurl.h"

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

void CommandQueue::NotifyStatusChanged(
    const fuchsia::mediaplayer::PlayerStatus& status) {
  // Process status received from the player.
  if (status.duration_ns != 0) {
    content_loaded_ = true;
    MaybeFinishWaitingForContentLoaded();
  }

  if (status.timeline_function) {
    timeline_function_ =
        fxl::To<media::TimelineFunction>(*status.timeline_function);
    MaybeScheduleWaitForPositionTask();
    MaybeFinishWaitingForSeekCompletion();
  }

  at_end_of_stream_ = status.end_of_stream;
  MaybeFinishWaitingForEndOfStream();
}

void CommandQueue::NotifyViewReady() {
  view_ready_ = true;
  MaybeFinishWaitingForViewReady();
}

void CommandQueue::MaybeFinishWaitingForContentLoaded() {
  if (content_loaded_ && wait_for_content_loaded_) {
    wait_for_content_loaded_ = false;

    if (verbose_) {
      std::cerr << "WaitForContentLoaded done\n";
    }

    ExecuteNextCommand();
  }
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

void CommandQueue::MaybeScheduleWaitForPositionTask() {
  if (wait_for_position_ != fuchsia::media::NO_TIMESTAMP) {
    wait_for_position_task_.Cancel();
    if (timeline_function_.invertable()) {
      // Apply the timeline function in reverse to find the CLOCK_MONOTONIC
      // time at which we should resume executing commands.
      int64_t wait_for_time =
          timeline_function_.ApplyInverse(wait_for_position_);
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

void CommandQueue::MaybeFinishWaitingForEndOfStream() {
  if (at_end_of_stream_ && wait_for_end_of_stream_) {
    wait_for_end_of_stream_ = false;

    if (verbose_) {
      std::cerr << "WaitForEndOfStream done\n";
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

  if (url.SchemeIsFile()) {
    auto fd = fxl::UniqueFD(open(url.path().c_str(), O_RDONLY));
    FXL_CHECK(fd.is_valid());
    command_queue->player_->SetFileSource(
        fsl::CloneChannelFromFileDescriptor(fd.get()));
  } else {
    command_queue->player_->SetHttpSource(url_, nullptr);
  }

  command_queue->prev_seek_position_ = 0;
  command_queue->at_end_of_stream_ = false;
  command_queue->ExecuteNextCommand();
}

void CommandQueue::SetFileCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "SetFile\n";
  }

  auto fd = fxl::UniqueFD(open(path_.c_str(), O_RDONLY));
  FXL_CHECK(fd.is_valid());
  command_queue->player_->SetFileSource(
      fsl::CloneChannelFromFileDescriptor(fd.get()));
  command_queue->prev_seek_position_ = 0;
  command_queue->at_end_of_stream_ = false;
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
  command_queue->at_end_of_stream_ = false;
  command_queue->ExecuteNextCommand();
}

void CommandQueue::InvokeCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "Invoke\n";
  }

  FXL_DCHECK(action_);
  action_();
  command_queue->ExecuteNextCommand();
}

void CommandQueue::WaitForContentLoadedCommand::Execute(
    CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "WaitForContentLoaded\n";
  }

  command_queue->wait_for_content_loaded_ = true;
  command_queue->MaybeFinishWaitingForContentLoaded();
}

void CommandQueue::WaitForViewReadyCommand::Execute(
    CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "WaitForViewReady\n";
  }

  command_queue->wait_for_view_ready_ = true;
  command_queue->MaybeFinishWaitingForViewReady();
}

void CommandQueue::WaitForPositionCommand::Execute(
    CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "WaitForPosition " << AsNs(position_.get()) << "\n";
  }

  command_queue->wait_for_position_ = position_.get();
  command_queue->MaybeScheduleWaitForPositionTask();
}

void CommandQueue::WaitForSeekCompletionCommand::Execute(
    CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "WaitForSeekCompletion\n";
  }

  command_queue->wait_for_seek_completion_position_ =
      command_queue->prev_seek_position_;
  command_queue->MaybeFinishWaitingForSeekCompletion();
}

void CommandQueue::WaitForEndOfStreamCommand::Execute(
    CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "WaitForEndOfStream\n";
  }

  command_queue->wait_for_end_of_stream_ = true;
  command_queue->MaybeFinishWaitingForEndOfStream();
}

void CommandQueue::SleepCommand::Execute(CommandQueue* command_queue) {
  if (command_queue->verbose_) {
    std::cerr << "Sleep " << AsNs(duration_.get()) << "\n";
  }

  async::PostDelayedTask(
      command_queue->dispatcher_,
      [command_queue]() { command_queue->ExecuteNextCommand(); },
      zx::duration(duration_));
}

}  // namespace test
}  // namespace media_player
