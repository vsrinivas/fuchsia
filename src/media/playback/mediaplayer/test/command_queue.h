// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_COMMAND_QUEUE_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_COMMAND_QUEUE_H_

#include <fuchsia/media/playback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include <queue>

#include "lib/media/cpp/timeline_function.h"
#include "src/lib/syslog/cpp/logger.h"

namespace media_player {
namespace test {

class CommandQueue {
 public:
  using StatusCondition = fit::function<bool(const fuchsia::media::playback::PlayerStatus&)>;

  CommandQueue();

  ~CommandQueue();

  void Init(fuchsia::media::playback::Player* player) { player_ = player; }

  void SetVerbose(bool verbose) { verbose_ = verbose; }

  // Executes the commands in the queue.
  void Execute() { ExecuteNextCommand(); }

  // Clears the command queue.
  void Clear() {
    wait_for_position_ = fuchsia::media::NO_TIMESTAMP;
    wait_for_status_condition_ = nullptr;
    status_ = nullptr;

    while (!command_queue_.empty()) {
      command_queue_.pop();
    }
  }

  // Notifies the command queue that player status has changed.
  void NotifyStatusChanged(const fuchsia::media::playback::PlayerStatus& status);

  // Notifies the command queue that the view is ready.
  void NotifyViewReady();

  // Queues a |SetFileSource| command.
  void SetUrl(const std::string& url) { AddCommand(new SetUrlCommand(url)); }

  // Queues a |SetFileSource| command.
  void SetFile(const std::string& path) { AddCommand(new SetFileCommand(path)); }

  // Queues a play command.
  void Play() { AddCommand(new PlayCommand()); }

  // Queues a pause command.
  void Pause() { AddCommand(new PauseCommand()); }

  // Queues a seek command.
  void Seek(zx::duration position) { AddCommand(new SeekCommand(position)); }
  void Seek(int64_t position) { Seek(zx::duration(position)); }

  // Queues a command that invokes |action|.
  void Invoke(fit::closure action) { AddCommand(new InvokeCommand(std::move(action))); }

  void WaitForStatusCondition(StatusCondition condition) {
    AddCommand(new WaitForStatusConditionCommand(std::move(condition)));
  }

  // Queues a command that waits until content is loaded.
  void WaitForContentLoaded() {
    WaitForStatusCondition(
        [](const fuchsia::media::playback::PlayerStatus& status) { return status.duration != 0; });
  }

  // Queues a command that waits until audio is connected.
  void WaitForAudioConnected() {
    WaitForStatusCondition([](const fuchsia::media::playback::PlayerStatus& status) {
      return status.audio_connected;
    });
  }

  // Queues a command that waits until video is connected.
  void WaitForVideoConnected() {
    WaitForStatusCondition([](const fuchsia::media::playback::PlayerStatus& status) {
      return status.video_connected;
    });
  }

  // Queues a command that waits until a problem is indicated.
  void WaitForProblem() {
    WaitForStatusCondition([](const fuchsia::media::playback::PlayerStatus& status) {
      return status.problem != nullptr;
    });
  }

  // Queues a command that waits util the view is ready.
  void WaitForViewReady() { AddCommand(new WaitForViewReadyCommand()); }

  // Queues a command that waits until the specified position is reached.
  void WaitForPosition(zx::duration position) { AddCommand(new WaitForPositionCommand(position)); }
  void WaitForPosition(int64_t position) { WaitForPosition(zx::duration(position)); }

  // Queues a command that waits a previous seek completes.
  void WaitForSeekCompletion() { AddCommand(new WaitForSeekCompletionCommand()); }

  // Queues a command that waits until end of stream is reached.
  void WaitForEndOfStream() {
    WaitForStatusCondition(
        [](const fuchsia::media::playback::PlayerStatus& status) { return status.end_of_stream; });
  }

  // Queues a command that sleeps for the specified duration.
  void Sleep(zx::duration duration) { AddCommand(new SleepCommand(duration)); }
  void Sleep(int64_t position) { Sleep(zx::duration(position)); }

 private:
  struct Command {
    virtual ~Command() = default;
    virtual void Execute(CommandQueue* command_queue) = 0;
  };

  struct SetUrlCommand : public Command {
    SetUrlCommand(const std::string& url) : url_(url) {}
    void Execute(CommandQueue* command_queue) override;
    std::string url_;
  };

  struct SetFileCommand : public Command {
    SetFileCommand(const std::string& path) : path_(path) {}
    void Execute(CommandQueue* command_queue) override;
    std::string path_;
  };

  struct PlayCommand : public Command {
    void Execute(CommandQueue* command_queue) override;
  };

  struct PauseCommand : public Command {
    void Execute(CommandQueue* command_queue) override;
  };

  struct SeekCommand : public Command {
    SeekCommand(zx::duration position) : position_(position) {}
    void Execute(CommandQueue* command_queue) override;
    zx::duration position_;
  };

  struct InvokeCommand : public Command {
    InvokeCommand(fit::closure action) : action_(std::move(action)) { FX_DCHECK(action_); }
    void Execute(CommandQueue* command_queue) override;
    fit::closure action_;
  };

  struct WaitForStatusConditionCommand : public Command {
    WaitForStatusConditionCommand(StatusCondition condition) : condition_(std::move(condition)) {
      FX_DCHECK(condition_);
    }
    void Execute(CommandQueue* command_queue) override;
    StatusCondition condition_;
  };

  struct WaitForViewReadyCommand : public Command {
    void Execute(CommandQueue* command_queue) override;
  };

  struct WaitForPositionCommand : public Command {
    WaitForPositionCommand(zx::duration position) : position_(position) {}
    void Execute(CommandQueue* command_queue) override;
    zx::duration position_;
  };

  struct WaitForSeekCompletionCommand : public Command {
    void Execute(CommandQueue* command_queue) override;
  };

  struct SleepCommand : public Command {
    SleepCommand(zx::duration duration) : duration_(duration) {}
    void Execute(CommandQueue* command_queue) override;
    zx::duration duration_;
  };

  // Finishes waiting for |wait_for_status_condition_| to succeed.
  void MaybeFinishWaitingForStatusCondition();

  // Finishes waiting for view ready if we're waiting for view ready and
  // if the view is ready.
  void MaybeFinishWaitingForViewReady();

  // Schedules a task to handle wait-for-position, as appropriate.
  void MaybeScheduleWaitForPositionTask();

  // Finishes waiting for seek completion if we're waiting for seek completion
  // and if the previous seek has completed.
  void MaybeFinishWaitingForSeekCompletion();

  // Finishes waiting for end of stream if we're waiting for end of stream and
  // if we're at end of stream.
  void MaybeFinishWaitingForEndOfStream();

  // Adds a command to the command queue.
  void AddCommand(Command* command) { command_queue_.emplace(command); }

  // Executes the next command in the queue, if any.
  void ExecuteNextCommand();

  async_dispatcher_t* dispatcher_;
  fuchsia::media::playback::Player* player_;
  std::queue<std::unique_ptr<Command>> command_queue_;
  media::TimelineFunction timeline_function_;
  std::unique_ptr<fuchsia::media::playback::PlayerStatus> status_;

  // This condition is polled in |NotifyStatusChanged| to determine if command
  // execution should be continued.
  StatusCondition wait_for_status_condition_;

  bool view_ready_ = false;
  bool wait_for_view_ready_ = false;

  int64_t prev_seek_position_ = fuchsia::media::NO_TIMESTAMP;
  int64_t wait_for_seek_completion_position_ = fuchsia::media::NO_TIMESTAMP;

  int64_t wait_for_position_ = fuchsia::media::NO_TIMESTAMP;
  async::TaskClosure wait_for_position_task_;

  bool verbose_ = false;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_COMMAND_QUEUE_H_
