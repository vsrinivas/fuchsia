// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/tool/inspect_command.h"

#include <fcntl.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <queue>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/tool/convert.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/eintr_wrapper.h"
#include "lib/fxl/files/file_descriptor.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/strings/string_number_conversions.h"

#define FILE_CREATE_MODE 0666

namespace tool {
namespace {
// When displaying value data, maximum number of bytes that will be displayed
// before being truncated.
const size_t kDataSizeLimit = 400;

// Returns a printable, truncated to kDataSizeLimit string from the argument.
std::string ToPrintable(fxl::StringView string) {
  for (char i : string) {
    // Check if the character is "normal" (printable, or new line) for the
    // current locale.
    if (!(isprint(i) || isspace(i))) {
      // Hex encoding takes 2 characters for each byte.
      if (string.size() > kDataSizeLimit / 2) {
        return convert::ToHex(string.substr(0, kDataSizeLimit / 2)) + "...";
      }
      return convert::ToHex(string);
    }
  }
  if (string.size() > kDataSizeLimit) {
    return string.substr(0, kDataSizeLimit).ToString() + "...";
  }
  return string.ToString();
}

class FileStreamWriter {
 public:
  explicit FileStreamWriter(const std::string& path)
      : fd_(HANDLE_EINTR(creat(path.c_str(), FILE_CREATE_MODE))) {}

  bool IsValid() { return fd_.is_valid(); }

  FileStreamWriter& operator<<(fxl::StringView str) {
    FXL_DCHECK(IsValid());
    bool result = fxl::WriteFileDescriptor(fd_.get(), str.data(), str.size());
    FXL_DCHECK(result);
    return *this;
  }

 private:
  fxl::UniqueFD fd_;
};

}  // namespace

InspectCommand::InspectCommand(std::vector<std::string> args)
    : args_(std::move(args)) {}

void InspectCommand::Start(fxl::Closure on_done) {
  if (args_.size() < 3) {
    PrintHelp(std::move(on_done));
    return;
  }
  user_repository_path_ = args_[1];
  app_id_ = args_[2];

  if (!files::IsDirectory(user_repository_path_)) {
    std::cerr << user_repository_path_ << " is not a directory" << std::endl;
    PrintHelp(std::move(on_done));
    return;
  }

  if (args_.size() == 4 && args_[3] == "pages") {
    ListPages(std::move(on_done));
  } else if (args_.size() == 6 && args_[3] == "commit") {
    DisplayCommit(std::move(on_done));
  } else if (args_.size() == 5 && args_[3] == "commit_graph") {
    DisplayCommitGraph(std::move(on_done));
  } else {
    PrintHelp(std::move(on_done));
  }
}

void InspectCommand::ListPages(fxl::Closure on_done) {
  std::cout << "List of pages for app " << app_id_ << ":" << std::endl;
  std::unique_ptr<storage::LedgerStorageImpl> ledger_storage(
      GetLedgerStorage());
  std::vector<storage::PageId> page_ids = ledger_storage->ListLocalPages();
  auto waiter = callback::CompletionWaiter::Create();
  for (const storage::PageId& page_id : page_ids) {
    ledger_storage->GetPageStorage(
        page_id, fxl::MakeCopyable([
          completer = fxl::MakeAutoCall(waiter->NewCallback()),
          page_id = page_id
        ](storage::Status status,
                 std::unique_ptr<storage::PageStorage> storage) mutable {
          if (status != storage::Status::OK) {
            FXL_LOG(FATAL) << "Unable to retrieve page "
                           << convert::ToHex(page_id) << " due to error "
                           << status;
          }
          storage->GetHeadCommitIds(fxl::MakeCopyable([
            completer = std::move(completer), page_id = std::move(page_id)
          ](storage::Status get_status, std::vector<storage::CommitId> heads) {
            std::cout << "Page " << convert::ToHex(page_id) << std::endl;
            if (get_status != storage::Status::OK) {
              FXL_LOG(FATAL)
                  << "Unable to retrieve commits for page "
                  << convert::ToHex(page_id) << " due to error " << get_status;
            }
            for (const storage::CommitId& commit_id : heads) {
              std::cout << " head commit " << convert::ToHex(commit_id)
                        << std::endl;
            }
          }));
        }));
  }
  waiter->Finalize(std::move(on_done));
}

void InspectCommand::DisplayCommit(fxl::Closure on_done) {
  std::unique_ptr<storage::LedgerStorageImpl> ledger_storage(
      GetLedgerStorage());
  storage::PageId page_id;
  if (!FromHexString(args_[4], &page_id)) {
    FXL_LOG(ERROR) << "Unable to parse page id " << args_[4];
    on_done();
    return;
  }
  storage::CommitId commit_id;
  if (!FromHexString(args_[5], &commit_id)) {
    FXL_LOG(ERROR) << "Unable to parse commit id " << args_[5];
    on_done();
    return;
  }

  ledger_storage->GetPageStorage(
      page_id, [ this, commit_id, on_done = std::move(on_done) ](
                   storage::Status status,
                   std::unique_ptr<storage::PageStorage> storage) mutable {
        if (status != storage::Status::OK) {
          FXL_LOG(ERROR) << "Unable to retrieve page due to error " << status;
          on_done();
          return;
        }
        storage_ = std::move(storage);
        storage_->GetCommit(
            commit_id,
            [ this, commit_id, on_done = std::move(on_done) ](
                storage::Status status,
                std::unique_ptr<const storage::Commit> commit) mutable {
              if (status != storage::Status::OK) {
                FXL_LOG(ERROR)
                    << "Unable to retrieve commit " << convert::ToHex(commit_id)
                    << " on page " << convert::ToHex(storage_->GetId())
                    << " due to error " << status;
                on_done();
                return;
              }
              PrintCommit(std::move(commit), std::move(on_done));
            });
      });
}

void InspectCommand::PrintCommit(std::unique_ptr<const storage::Commit> commit,
                                 fxl::Closure on_done) {
  // Print commit info
  std::cout << "Commit " << args_[4] << std::endl;
  std::cout << " timestamp " << commit->GetTimestamp() << std::endl;
  for (const storage::CommitIdView& parent_commit : commit->GetParentIds()) {
    std::cout << " parent " << convert::ToHex(parent_commit) << std::endl;
  }
  std::cout << "Page state at this commit: " << std::endl;
  coroutine_service_.StartCoroutine(fxl::MakeCopyable([
    this, commit = std::move(commit), on_done = std::move(on_done)
  ](coroutine::CoroutineHandler * handler) mutable {
    storage_->GetCommitContents(
        *commit, "",
        [this, handler](storage::Entry entry) {
          storage::Status status;
          std::unique_ptr<const storage::Object> object;
          if (coroutine::SyncCall(
                  handler,
                  [this, &entry](
                      std::function<void(
                          storage::Status,
                          std::unique_ptr<const storage::Object>)> callback) {
                    storage_->GetObject(entry.object_id,
                                        storage::PageStorage::Location::LOCAL,
                                        std::move(callback));
                  },
                  &status, &object)) {
            FXL_NOTREACHED();
          }
          std::string priority_str =
              entry.priority == storage::KeyPriority::EAGER ? "EAGER" : "LAZY";
          fxl::StringView data;
          object->GetData(&data);
          std::cout << " Key " << entry.key << " (" << priority_str << "): ";
          std::cout << ToPrintable(data) << std::endl;
          return true;
        },
        [on_done = std::move(on_done)](storage::Status status) {
          if (status != storage::Status::OK) {
            FXL_LOG(FATAL) << "Unable to retrieve commit contents due to error "
                           << status;
          }
          on_done();
        });
  }));
}

void InspectCommand::DisplayCommitGraph(fxl::Closure on_done) {
  std::unique_ptr<storage::LedgerStorageImpl> ledger_storage(
      GetLedgerStorage());
  storage::PageId page_id;
  if (!FromHexString(args_[4], &page_id)) {
    FXL_LOG(ERROR) << "Unable to parse page id " << args_[4];
    on_done();
    return;
  }
  ledger_storage->GetPageStorage(
      page_id, [ this, page_id, on_done = std::move(on_done) ](
                   storage::Status status,
                   std::unique_ptr<storage::PageStorage> storage) mutable {
        if (status != storage::Status::OK) {
          FXL_LOG(ERROR) << "Unable to retrieve page due to error " << status;
          on_done();
          return;
        }
        storage_ = std::move(storage);
        coroutine_service_.StartCoroutine(fxl::MakeCopyable([
          this, page_id = std::move(page_id), on_done = std::move(on_done)
        ](coroutine::CoroutineHandler * handler) mutable {
          DisplayGraphCoroutine(handler, page_id, std::move(on_done));
        }));
      });
}

void InspectCommand::DisplayGraphCoroutine(coroutine::CoroutineHandler* handler,
                                           storage::PageId page_id,
                                           fxl::Closure on_done) {
  storage::Status status;
  std::vector<std::unique_ptr<const storage::Commit>> unsynced_commits;
  if (coroutine::SyncCall(
          handler,
          [this](std::function<void(
                     storage::Status,
                     std::vector<std::unique_ptr<const storage::Commit>>)>
                     callback) {
            storage_->GetUnsyncedCommits(std::move(callback));
          },
          &status, &unsynced_commits)) {
    FXL_NOTREACHED();
  }

  std::unordered_set<storage::CommitId> unsynced_commit_ids;
  std::for_each(unsynced_commits.begin(), unsynced_commits.end(),
                [&unsynced_commit_ids](
                    const std::unique_ptr<const storage::Commit>& commit) {
                  unsynced_commit_ids.insert(commit->GetId());
                });

  std::vector<storage::CommitId> heads;
  if (coroutine::SyncCall(
          handler,
          [this](std::function<void(storage::Status,
                                    std::vector<storage::CommitId>)> callback) {
            storage_->GetHeadCommitIds(std::move(callback));
          },
          &status, &heads)) {
    FXL_NOTREACHED();
  }
  std::unordered_set<storage::CommitId> commit_ids;
  std::deque<storage::CommitId> to_explore;
  if (status != storage::Status::OK) {
    FXL_LOG(FATAL) << "Unable to get head commits due to error " << status;
  }

  commit_ids.insert(heads.begin(), heads.end());
  to_explore.insert(to_explore.begin(), heads.begin(), heads.end());

  std::string normalized_app_id(app_id_);
  std::replace(normalized_app_id.begin(), normalized_app_id.end(), '/', '_');
  std::replace(normalized_app_id.begin(), normalized_app_id.end(), ':', '_');

  std::string file_path =
      "/tmp/" + normalized_app_id + "_" + convert::ToHex(page_id) + ".dot";
  FileStreamWriter writer(file_path);
  writer << "digraph P_" << convert::ToHex(page_id) << " {\n";
  while (!to_explore.empty()) {
    storage::CommitId commit_id = to_explore.front();
    to_explore.pop_front();
    storage::Status status;
    std::unique_ptr<const storage::Commit> commit;
    if (coroutine::SyncCall(
            handler,
            [this, &commit_id](
                std::function<void(storage::Status,
                                   std::unique_ptr<const storage::Commit>)>
                    callback) {
              storage_->GetCommit(commit_id, std::move(callback));
            },
            &status, &commit)) {
      FXL_NOTREACHED();
    }
    std::vector<storage::CommitIdView> parents = commit->GetParentIds();
    for (const storage::CommitIdView& parent : parents) {
      storage::CommitId parent_id = parent.ToString();
      if (commit_ids.count(parent_id) != 1) {
        commit_ids.insert(parent_id);
        to_explore.push_back(parent_id);
      }

      writer << "C_" << convert::ToHex(parent) << " -> "
             << "C_" << convert::ToHex(commit_id) << ";\n";
    }

    writer << "C_" << convert::ToHex(commit_id) << " [";
    if (parents.size() == 2) {
      writer << "shape=box, ";
    }
    if (unsynced_commit_ids.count(commit_id) != 0) {
      writer << "bgcolor=red, ";
    }
    writer << "tooltip=\"timestamp="
           << fxl::NumberToString(commit->GetTimestamp())
           << " root_id=" << convert::ToHex(commit->GetRootId()) << "\"];\n";
  }
  writer << "}\n";
  std::cout << "Graph of commits stored in file " << file_path << std::endl;
  on_done();
}

void InspectCommand::PrintHelp(fxl::Closure on_done) {
  std::cout
      << "inspect command: inspects the contents of a ledger.\n"
      << "Note: you must stop Ledger before running this tool.\n\n"
      << "Syntax: ledger_tool inspect <ledger repository path> "
         "<app_id> (pages|commit <page_id> <commit_id>)\n\n"
      << "Parameters:\n"
      << " - app_id: ID of the application to inspect\n"
      << "           e.g.: modular_user_runner\n"
      << " - pages: list all pages available locally, with their head commits\n"
      << " - commit <page_id> <commit_id>: list the full contents at the "
         "commit from the given page.\n"
      << " - commit_graph <page_id>: write the commit graph as a dot file."
      << std::endl;
  on_done();
}

std::unique_ptr<storage::LedgerStorageImpl> InspectCommand::GetLedgerStorage() {
  return std::make_unique<storage::LedgerStorageImpl>(
      &coroutine_service_, user_repository_path_, app_id_);
}

}  // namespace tool
