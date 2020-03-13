// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/story_puppet_master_impl.h"

#include <utility>

#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/syslog/cpp/logger.h"
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
    session_storage_->GetStoryData(story_name_)
        ->WeakThen(GetWeakPtr(), [this](fuchsia::modular::internal::StoryDataPtr data) {
          if (data) {
            story_id_ = data->story_info().id();
            ExecuteCommands();
            return;
          }

          CreateStory();
        });
  }

  void CreateStory() {
    session_storage_->CreateStory(story_name_, /*annotations=*/{})
        ->WeakThen(GetWeakPtr(), [this](fidl::StringPtr story_id, auto /* ignored */) {
          story_id_ = std::move(story_id);
          ExecuteCommands();
        });
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

  fidl::StringPtr story_id_;
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

    session_storage_->GetStoryData(story_name_)
        ->WeakThen(GetWeakPtr(), [this](fuchsia::modular::internal::StoryDataPtr data) {
          if (data) {
            Annotate(std::move(data));
          } else {
            CreateStory();
          }
        });
  }

  void CreateStory() {
    if (annotations_.size() > fuchsia::modular::MAX_ANNOTATIONS_PER_STORY) {
      fuchsia::modular::StoryPuppetMaster_Annotate_Result result{};
      result.set_err(fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS);
      Done(std::move(result));
      return;
    }

    session_storage_->CreateStory(story_name_, std::move(annotations_))
        ->WeakThen(GetWeakPtr(), [this](fidl::StringPtr story_id, auto /* ignored */) {
          fuchsia::modular::StoryPuppetMaster_Annotate_Result result{};
          result.set_response({});
          Done(std::move(result));
        });
  }

  void Annotate(fuchsia::modular::internal::StoryDataPtr story_data) {
    // Merge the annotations provided to the operation into any existing ones in `story_data`.
    auto new_annotations =
        story_data->story_info().has_annotations()
            ? annotations::Merge(
                  std::move(*story_data->mutable_story_info()->mutable_annotations()),
                  std::move(annotations_))
            : std::move(annotations_);

    if (new_annotations.size() > fuchsia::modular::MAX_ANNOTATIONS_PER_STORY) {
      fuchsia::modular::StoryPuppetMaster_Annotate_Result result{};
      result.set_err(fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS);
      Done(std::move(result));
      return;
    }

    session_storage_->UpdateStoryAnnotations(story_name_, std::move(new_annotations))
        ->WeakThen(GetWeakPtr(), [this]() {
          fuchsia::modular::StoryPuppetMaster_Annotate_Result result{};
          result.set_response({});
          Done(std::move(result));
        });
  }

  SessionStorage* const session_storage_;
  std::string story_name_;
  std::vector<fuchsia::modular::Annotation> annotations_;
};

class AnnotateModuleOperation
    : public Operation<fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result> {
 public:
  AnnotateModuleOperation(SessionStorage* const session_storage, std::string story_name,
                          std::string module_id,
                          std::vector<fuchsia::modular::Annotation> annotations, ResultCall done)
      : Operation("StoryPuppetMasterImpl.AnnotateModuleOperation", std::move(done)),
        session_storage_(session_storage),
        story_name_(std::move(story_name)),
        module_id_(std::move(module_id)),
        annotations_(std::move(annotations)) {}

 private:
  void Run() override {
    for (auto const& annotation : annotations_) {
      if (annotation.value && annotation.value->is_buffer() &&
          annotation.value->buffer().size >
              fuchsia::modular::MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES) {
        fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result{};
        result.set_err(fuchsia::modular::AnnotationError::VALUE_TOO_BIG);
        Done(std::move(result));
        return;
      }
    }

    session_storage_->GetStoryStorage(story_name_)
        ->WeakAsyncMap(
            GetWeakPtr(),
            [this](std::unique_ptr<StoryStorage> story_storage) {
              if (story_storage == nullptr) {
                // Since Modules are created by an external component, and the external component
                // would only be able to add a Module to a Story that it managed, it isn't possible
                // for AnnotateModule() to create it's own StoryStorage, if not found. So this is
                // an error.
                fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result{};
                result.set_err(fuchsia::modular::AnnotationError::NOT_FOUND);
                Done(std::move(result));
                return static_cast<FuturePtr<fuchsia::modular::ModuleDataPtr>>(nullptr);
              }
              story_storage_ = std::move(story_storage);
              // Add a callback on module_data updates before attempting ReadModuleData,
              // then try to ReadModuleData(), if it exists in |StoryStorage|.
              // If the module_data is not found, the callback will be called if and
              // when the ModuleData is stored, later.
              story_storage_->set_on_module_data_updated(
                  [&](fuchsia::modular::ModuleData new_module_data) {
                    if (new_module_data.module_path().back() == module_id_) {
                      AnnotateModuleIfFirstAttempt(std::make_unique<fuchsia::modular::ModuleData>(
                          std::move(new_module_data)));
                    }
                  });
              return story_storage_->ReadModuleData({module_id_})
                  ->WeakMap(GetWeakPtr(), [](fuchsia::modular::ModuleDataPtr module_data) mutable {
                    return module_data;
                  });
            })
        ->WeakThen(GetWeakPtr(), [this](fuchsia::modular::ModuleDataPtr module_data) mutable {
          if (module_data != nullptr) {
            AnnotateModuleIfFirstAttempt(std::move(module_data));
          }
        });
  }

  void AnnotateModuleIfFirstAttempt(fuchsia::modular::ModuleDataPtr module_data) {
    if (attempted)
      return;
    attempted = true;
    // Merge the annotations provided to the operation into any existing ones in `module_data`.
    auto new_annotations = module_data->has_annotations()
                               ? annotations::Merge(std::move(*module_data->mutable_annotations()),
                                                    std::move(annotations_))
                               : std::move(annotations_);

    if (new_annotations.size() > fuchsia::modular::MAX_ANNOTATIONS_PER_MODULE) {
      fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result{};
      result.set_err(fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS);
      Done(std::move(result));
      return;
    }

    // Save the new version of module_data with annotations added.
    module_data->set_annotations(std::move(new_annotations));
    story_storage_->WriteModuleData(std::move(*module_data))->Then([this]() mutable {
      fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result{};
      result.set_response({});
      Done(std::move(result));
    });
  }

  SessionStorage* const session_storage_;
  std::unique_ptr<StoryStorage> story_storage_;
  std::string story_name_;
  std::string module_id_;
  std::vector<fuchsia::modular::Annotation> annotations_;
  bool attempted = false;
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

void StoryPuppetMasterImpl::AnnotateModule(std::string module_id,
                                           std::vector<fuchsia::modular::Annotation> annotations,
                                           AnnotateModuleCallback callback) {
  operations_->Add(std::make_unique<AnnotateModuleOperation>(
      session_storage_, story_name_, module_id, std::move(annotations), std::move(callback)));
}

}  // namespace modular
