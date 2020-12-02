// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/story_puppet_master_impl.h"

#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "src/lib/fsl/types/type_converters.h"
#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/puppet_master/story_command_executor.h"
#include "src/modular/bin/sessionmgr/storage/session_storage.h"

namespace modular {

namespace {

class ExecuteOperation : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  ExecuteOperation(SessionStorage* const session_storage, StoryCommandExecutor* const executor,
                   std::string story_name, std::vector<fuchsia::modular::StoryCommand> commands,
                   ResultCall done)
      : Operation("StoryPuppetMasterImpl.ExecuteOperation", std::move(done)),
        session_storage_(session_storage),
        executor_(executor),
        story_name_(std::move(story_name)),
        commands_(std::move(commands)) {}

 private:
  void Run() override {
    auto data = session_storage_->GetStoryData(story_name_);
    if (data) {
      story_id_ = data->story_info().id();
      ExecuteCommands();
      return;
    }

    story_id_ = session_storage_->CreateStory(story_name_, /*annotations=*/{});
    ExecuteCommands();
  }

  void ExecuteCommands() {
    executor_->ExecuteCommands(
        story_id_, std::move(commands_),
        [weak_ptr = GetWeakPtr(), this](fuchsia::modular::ExecuteResult result) {
          Done(std::move(result));
        });
  }

  SessionStorage* const session_storage_;
  StoryCommandExecutor* const executor_;
  std::string story_name_;
  std::vector<fuchsia::modular::StoryCommand> commands_;

  std::string story_id_;
};

class AnnotateOperation : public Operation<fuchsia::modular::StoryPuppetMaster_Annotate_Result> {
 public:
  AnnotateOperation(SessionStorage* const session_storage, std::string story_name,
                    std::vector<fuchsia::modular::Annotation> annotations, ResultCall done)
      : Operation("StoryPuppetMasterImpl.AnnotateOperation", std::move(done)),
        session_storage_(session_storage),
        story_name_(std::move(story_name)),
        annotations_(std::move(annotations)) {}

 private:
  void Run() override {
    auto data = session_storage_->GetStoryData(story_name_);
    if (data) {
      MergeAnnotations(std::move(data));
    } else {
      CreateStory();
    }
  }

  void CreateStory() {
    // Ensure that none of the annotations are too big.
    for (auto const& annotation : annotations_) {
      if (annotation.value && annotation.value->is_buffer() &&
          annotation.value->buffer().size >
              fuchsia::modular::MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES) {
        fuchsia::modular::StoryPuppetMaster_Annotate_Result result{};
        result.set_err(fuchsia::modular::AnnotationError::VALUE_TOO_BIG);
        Done(std::move(result));
        return;
      }
    }

    // Ensure that the number of annotations does not exceed the limit per story.
    if (annotations_.size() > fuchsia::modular::MAX_ANNOTATIONS_PER_STORY) {
      fuchsia::modular::StoryPuppetMaster_Annotate_Result result{};
      result.set_err(fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS);
      Done(std::move(result));
      return;
    }

    session_storage_->CreateStory(story_name_, std::move(annotations_));
    fuchsia::modular::StoryPuppetMaster_Annotate_Result result{};
    result.set_response({});
    Done(std::move(result));
  }

  void MergeAnnotations(fuchsia::modular::internal::StoryDataPtr story_data) {
    fuchsia::modular::StoryPuppetMaster_Annotate_Result result{};

    auto merge_error =
        session_storage_->MergeStoryAnnotations(story_name_, std::move(annotations_));
    if (merge_error) {
      result.set_err(merge_error.value());
    } else {
      result.set_response({});
    }
    Done(std::move(result));
  }

  // Not owned.
  SessionStorage* const session_storage_;

  std::string story_name_;
  std::vector<fuchsia::modular::Annotation> annotations_;
};

// Responds to a |WatchAnnotations| request with the current annotations, if any.
class GetAnnotationsOperation
    : public Operation<fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Result> {
 public:
  GetAnnotationsOperation(SessionStorage* const session_storage, std::string story_name,
                          ResultCall done)
      : Operation("StoryPuppetMasterImpl.GetAnnotationsOperation", std::move(done)),
        session_storage_(session_storage),
        story_name_(std::move(story_name)) {}

 private:
  void Run() override {
    fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Result result{};

    auto data = session_storage_->GetStoryData(story_name_);
    if (data) {
      fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Response response{};
      response.annotations = std::move(*data->mutable_story_info()->mutable_annotations());
      result.set_response(std::move(response));
    } else {
      result.set_err(fuchsia::modular::AnnotationError::NOT_FOUND);
    }

    Done(std::move(result));
  }

  // Not owned.
  SessionStorage* const session_storage_;

  std::string story_name_;
};

}  // namespace

StoryPuppetMasterImpl::StoryPuppetMasterImpl(std::string story_name,
                                             OperationContainer* const operations,
                                             SessionStorage* const session_storage,
                                             StoryCommandExecutor* const executor)
    : story_name_(std::move(story_name)),
      session_storage_(session_storage),
      executor_(executor),
      operations_(operations),
      weak_ptr_factory_(this) {
  FX_DCHECK(session_storage != nullptr);
  FX_DCHECK(executor != nullptr);
}

StoryPuppetMasterImpl::~StoryPuppetMasterImpl() = default;

void StoryPuppetMasterImpl::Enqueue(std::vector<fuchsia::modular::StoryCommand> commands) {
  enqueued_commands_.insert(enqueued_commands_.end(), make_move_iterator(commands.begin()),
                            make_move_iterator(commands.end()));
}

void StoryPuppetMasterImpl::Execute(ExecuteCallback done) {
  operations_->Add(std::make_unique<ExecuteOperation>(
      session_storage_, executor_, story_name_, std::move(enqueued_commands_), std::move(done)));
}

void StoryPuppetMasterImpl::SetStoryInfoExtra(
    std::vector<fuchsia::modular::StoryInfoExtraEntry> story_info_extra,
    SetStoryInfoExtraCallback callback) {
  // This method is a no-op.
  fuchsia::modular::StoryPuppetMaster_SetStoryInfoExtra_Result result{};
  result.set_response({});
  callback(std::move(result));
}

void StoryPuppetMasterImpl::Annotate(std::vector<fuchsia::modular::Annotation> annotations,
                                     AnnotateCallback callback) {
  operations_->Add(std::make_unique<AnnotateOperation>(
      session_storage_, story_name_, std::move(annotations), std::move(callback)));
}

void StoryPuppetMasterImpl::WatchAnnotations(WatchAnnotationsCallback callback) {
  if (!watch_annotations_called_) {
    watch_annotations_called_ = true;
    operations_->Add(std::make_unique<GetAnnotationsOperation>(session_storage_, story_name_,
                                                               std::move(callback)));
    return;
  }

  session_storage_->SubscribeAnnotationsUpdated(
      [story_name = story_name_, callback = std::move(callback)](
          std::string story_id, const std::vector<fuchsia::modular::Annotation>& annotations,
          const std::set<std::string>& /*annotation_keys_added*/,
          const std::set<std::string>& /*annotation_keys_deleted*/) {
        if (story_id != story_name) {
          return WatchInterest::kContinue;
        }
        fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Response response{};
        response.annotations = fidl::Clone(annotations);
        fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Result result{};
        result.set_response(std::move(response));
        callback(std::move(result));
        return WatchInterest::kStop;
      });
}

}  // namespace modular
