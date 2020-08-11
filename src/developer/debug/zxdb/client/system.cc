// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/system.h"

#include <filesystem>
#include <set>

#include "src/developer/debug/shared/logging/debug.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/breakpoint_impl.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/job.h"
#include "src/developer/debug/zxdb/client/process_impl.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/symbol_server.h"
#include "src/developer/debug/zxdb/client/system_observer.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/client/thread_impl.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/vector_register_format.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_status.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

// Schema definition -------------------------------------------------------------------------------

const char* ClientSettings::System::kAutoCastToDerived = "auto-cast-to-derived";
static const char* kAutoCastToDerivedDescription =
    R"(  Automatically cast pointers and references to the final derived class when
  possible.

  When a class has virtual members, zxdb can use the vtable information to
  deduce the specific derived class for the object. This affects printing and
  the resolution of class/struct members in expressions.)";

const char* ClientSettings::System::kDebugMode = "debug-mode";
static const char* kDebugModeDescription =
    R"(  Output debug information about zxdb.
  In general should only be useful for people developing zxdb.)";

const char* ClientSettings::System::kPauseOnLaunch = "pause-on-launch";
static const char* kPauseOnLaunchDescription =
    R"(  Whether a process launched through zxdb should be stopped on startup.
  This will also affect components launched through zxdb.)";

const char* ClientSettings::System::kPauseOnAttach = "pause-on-attach";
static const char* kPauseOnAttachDescription =
    R"(  Whether the process should be paused when zxdb attached to it.
  This will also affect when zxdb attached a process through a filter.)";

const char* ClientSettings::System::kShowFilePaths = "show-file-paths";
static const char* kShowFilePathsDescription =
    R"(  Displays full path information when file names are displayed. Otherwise
  file names will be shortened to the shortest unique name in the current
  process.)";

const char* ClientSettings::System::kShowStdout = "show-stdout";
static const char* kShowStdoutDescription =
    R"(  Whether newly debugged process (either launched or attached) should
  output it's stdout/stderr to zxdb. This setting is global but can be overridden
  by each individual process.)";

const char* ClientSettings::System::kQuitAgentOnExit = "quit-agent-on-exit";
static const char* kQuitAgentOnExitDescription =
    R"(  Whether the client will shutdown the connected agent upon exiting.")";

const char* ClientSettings::System::kLanguage = "language";
static const char* kLanguageDescription =
    R"(  Programming language for expressions given to commands such as print.
  Valid values are "c++", "rust", and "auto". Most of the time you'll want to
  set this to "auto" and let zxdb determine the language of the current unit.)";
const char* ClientSettings::System::kLanguage_Cpp = "c++";
const char* ClientSettings::System::kLanguage_Rust = "rust";
const char* ClientSettings::System::kLanguage_Auto = "auto";

// Symbol lookup.
const char* ClientSettings::System::kSymbolIndexFiles = "symbol-index-files";
static const char* kSymbolIndexFilesDescription =
    R"(  List of symbol-index files for symbol lookup. The content will be used
  to populate the "ids-txts" and "build-id-dirs" settings. Check the
  "symbol-index" host tool for more information.)";

const char* ClientSettings::System::kSymbolPaths = "symbol-paths";
static const char* kSymbolPathsDescription =
    R"(  List of ELF files or directories for symbol lookup. When a directory
  path is passed, the directory will be enumerated non-recursively to index all
  ELF files within. When a file is passed, it will be loaded as an ELF file.)";

const char* ClientSettings::System::kBuildIdDirs = "build-id-dirs";
static const char* kBuildIdDirsDescription =
    R"(  List of ".build-id" directories for symbol lookup. Each directory is assumed to
  contain a ".build-id"-style index of symbol files, that is, each symbol file
  lives at xx/yyyyyyyy.debug where xx is the first two characters of the build
  ID and yyyyyyyy is the rest. However, the name of the directory doesn't need
  to be .build-id.)";

const char* ClientSettings::System::kIdsTxts = "ids-txts";
static const char* kIdsTxtsDescription =
    R"(  List of "ids.txt" files for symbol lookup. Each file, typically named
  "ids.txt", serves as a mapping from build ID to symbol file path and should
  contain multiple lines in the format of "<build ID> <file path>".)";

const char* ClientSettings::System::kSymbolServers = "symbol-servers";
static const char* kSymbolServersDescription = R"(  List of symbol server URLs.)";

const char* ClientSettings::System::kSymbolCache = "symbol-cache";
static const char* kSymbolCacheDescription =
    R"(  Directory where we can keep a symbol cache. If a symbol server has been
  specified, downloaded symbols will be stored in this directory. The directory
  structure will be the same as a .build-id directory.)";

namespace {

fxl::RefPtr<SettingSchema> CreateSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  schema->AddBool(ClientSettings::System::kAutoCastToDerived, kAutoCastToDerivedDescription, true);
  schema->AddBool(ClientSettings::System::kDebugMode, kDebugModeDescription, false);
  schema->AddBool(ClientSettings::System::kPauseOnLaunch, kPauseOnLaunchDescription, false);
  schema->AddBool(ClientSettings::System::kPauseOnAttach, kPauseOnAttachDescription, false);
  schema->AddBool(ClientSettings::System::kQuitAgentOnExit, kQuitAgentOnExitDescription, false);
  schema->AddBool(ClientSettings::System::kShowFilePaths, kShowFilePathsDescription, false);
  schema->AddBool(ClientSettings::System::kShowStdout, kShowStdoutDescription, true);
  schema->AddString(ClientSettings::System::kLanguage, kLanguageDescription, "auto",
                    {"rust", "c++", "auto"});

  // Symbol lookup.
  schema->AddList(ClientSettings::System::kSymbolIndexFiles, kSymbolIndexFilesDescription, {});
  schema->AddList(ClientSettings::System::kSymbolPaths, kSymbolPathsDescription, {});
  schema->AddList(ClientSettings::System::kBuildIdDirs, kBuildIdDirsDescription, {});
  schema->AddList(ClientSettings::System::kIdsTxts, kIdsTxtsDescription, {});
  schema->AddList(ClientSettings::System::kSymbolServers, kSymbolServersDescription, {});
  schema->AddString(ClientSettings::System::kSymbolCache, kSymbolCacheDescription, "");

  // Target ones.
  schema->AddList(ClientSettings::Target::kBuildDirs, ClientSettings::Target::kBuildDirsDescription,
                  {});
  schema->AddString(
      ClientSettings::Target::kVectorFormat, ClientSettings::Target::kVectorFormatDescription,
      kVectorRegisterFormatStr_Double, ClientSettings::Target::GetVectorFormatOptions());

  // Thread ones.
  schema->AddBool(ClientSettings::Thread::kDebugStepping,
                  ClientSettings::Thread::kDebugSteppingDescription, false);
  schema->AddList(ClientSettings::Thread::kDisplay, ClientSettings::Thread::kDisplayDescription);

  return schema;
}

}  // namespace

// Download ----------------------------------------------------------------------------------------

// When we want to download symbols for a build ID, we create a Download object. We then fire off
// requests to all symbol servers we know about asking whether they have the symbols we need. These
// requests are async, and the callbacks each own a shared_ptr to the Download object. If all the
// callbacks run and none of them are informed the request was successful, all of the shared_ptrs
// are dropped and the Download object is freed. The destructor of the Download object calls a
// callback that handles notifying the rest of the system of those results.
//
// If one of the callbacks does report that the symbols were found, a transaction to actually start
// the download is initiated, and its reply callback is again given a shared_ptr to the download. If
// we receive more notifications that other servers also have the symbol in the meantime, they are
// queued and will be tried as a fallback if the download fails. Again, once the download callback
// runs the shared_ptr is dropped, and when the Download object dies the destructor handles
// notifying the system.
class Download {
 public:
  explicit Download(const std::string& build_id, DebugSymbolFileType file_type,
                    SymbolServer::FetchCallback result_cb)
      : build_id_(build_id), file_type_(file_type), result_cb_(std::move(result_cb)) {}

  ~Download() { Finish(); }

  bool active() { return !!result_cb_; }

  // Notify this download object that we have gotten the symbols if we're going to get them.
  void Finish();

  // Notify this Download object that one of the servers has the symbols available.
  void Found(std::shared_ptr<Download> self, fit::callback<void(SymbolServer::FetchCallback)>);

  // Notify this Download object that a transaction failed.
  void Error(std::shared_ptr<Download> self, const Err& err);

  // Add a symbol server to this download.
  void AddServer(std::shared_ptr<Download> self, SymbolServer* server);

 private:
  void RunCB(std::shared_ptr<Download> self, fit::callback<void(SymbolServer::FetchCallback)>& cb);

  std::string build_id_;
  DebugSymbolFileType file_type_;
  Err err_;
  std::string path_;
  SymbolServer::FetchCallback result_cb_;
  std::vector<fit::callback<void(SymbolServer::FetchCallback)>> server_cbs_;
  bool trying_ = false;
};

void Download::Finish() {
  if (!result_cb_)
    return;

  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [result_cb = std::move(result_cb_), err = std::move(err_),
                  path = std::move(path_)]() mutable { result_cb(err, path); });

  result_cb_ = nullptr;
}

void Download::AddServer(std::shared_ptr<Download> self, SymbolServer* server) {
  FX_DCHECK(self.get() == this);

  if (!result_cb_)
    return;

  server->CheckFetch(build_id_, file_type_,
                     [self](const Err& err, fit::callback<void(SymbolServer::FetchCallback)> cb) {
                       if (!cb)
                         self->Error(self, err);
                       else
                         self->Found(self, std::move(cb));
                     });
}

void Download::Found(std::shared_ptr<Download> self,
                     fit::callback<void(SymbolServer::FetchCallback)> cb) {
  FX_DCHECK(self.get() == this);

  if (!result_cb_)
    return;

  if (trying_) {
    server_cbs_.push_back(std::move(cb));
    return;
  }

  RunCB(self, cb);
}

void Download::Error(std::shared_ptr<Download> self, const Err& err) {
  FX_DCHECK(self.get() == this);

  if (!result_cb_)
    return;

  if (!err_.has_error()) {
    err_ = err;
  } else if (err.has_error()) {
    err_ = Err("Multiple servers could not be reached.");
  }

  if (!trying_ && !server_cbs_.empty()) {
    RunCB(self, server_cbs_.back());
    server_cbs_.pop_back();
  }
}

void Download::RunCB(std::shared_ptr<Download> self,
                     fit::callback<void(SymbolServer::FetchCallback)>& cb) {
  FX_DCHECK(!trying_);
  trying_ = true;

  cb([self](const Err& err, const std::string& path) {
    self->trying_ = false;

    if (path.empty()) {
      self->Error(self, err);
    } else {
      self->err_ = err;
      self->path_ = path;
      self->Finish();
    }
  });
}

// System Implementation ---------------------------------------------------------------------------

System::System(Session* session)
    : ClientObject(session), symbols_(this), settings_(GetSchema(), nullptr), weak_factory_(this) {
  // Create the default job and target.
  AddNewJob(std::make_unique<Job>(session, true));
  AddNewTarget(std::make_unique<TargetImpl>(this));

  // Forward all messages from the symbol index to our observers. It's OK to bind |this| because the
  // symbol index is owned by |this|.
  symbols_.build_id_index().set_information_callback([this](const std::string& msg) {
    for (auto& observer : observers_)
      observer.OnSymbolIndexingInformation(msg);
  });

  // The system is the one holding the system symbols and is the one who will be updating the
  // symbols once we get a symbol change, so the System will be listening to its own options. We
  // don't use SystemSymbols because they live in the symbols library and we don't want it to have a
  // client dependency.
  settings_.AddObserver(ClientSettings::System::kDebugMode, this);
  settings_.AddObserver(ClientSettings::System::kSymbolIndexFiles, this);
  settings_.AddObserver(ClientSettings::System::kSymbolCache, this);
  settings_.AddObserver(ClientSettings::System::kSymbolPaths, this);
  settings_.AddObserver(ClientSettings::System::kBuildIdDirs, this);
  settings_.AddObserver(ClientSettings::System::kIdsTxts, this);
  settings_.AddObserver(ClientSettings::System::kSymbolServers, this);

  // Observe the session for filter matches and attach to any process koid that the system is not
  // already attached to.
  session->AddFilterObserver(this);
}

System::~System() {
  // Target destruction may depend on the symbol system. Ensure the targets get cleaned up first.
  for (auto& target : targets_) {
    // It's better if process destruction notifications are sent before target ones because the
    // target owns the process. Because this class sends the target notifications, force the process
    // destruction before doing anything.
    target->ImplicitlyDetach();
    for (auto& observer : session()->target_observers())
      observer.WillDestroyTarget(target.get());
  }

  targets_.clear();

  // Filters list may be iterated as we clean them up. Move its contents here first then let it
  // drop so the dying objects are out of the system.
  auto filters = std::move(filters_);
}

fxl::RefPtr<SettingSchema> System::GetSchema() {
  // Will only run initialization once.
  InitializeSchemas();
  static fxl::RefPtr<SettingSchema> schema = CreateSchema();
  return schema;
}

ProcessImpl* System::ProcessImplFromKoid(uint64_t koid) const {
  for (const auto& target : targets_) {
    ProcessImpl* process = target->process();
    if (process && process->GetKoid() == koid)
      return process;
  }
  return nullptr;
}

std::vector<TargetImpl*> System::GetTargetImpls() const {
  std::vector<TargetImpl*> result;
  for (const auto& t : targets_)
    result.push_back(t.get());
  return result;
}

TargetImpl* System::CreateNewTargetImpl(TargetImpl* clone) {
  auto target = clone ? clone->Clone(this) : std::make_unique<TargetImpl>(this);
  TargetImpl* to_return = target.get();
  AddNewTarget(std::move(target));
  return to_return;
}

SystemSymbols* System::GetSymbols() { return &symbols_; }

std::vector<Target*> System::GetTargets() const {
  std::vector<Target*> result;
  result.reserve(targets_.size());
  for (const auto& t : targets_)
    result.push_back(t.get());
  return result;
}

std::vector<Job*> System::GetJobs() const {
  std::vector<Job*> result;
  result.reserve(jobs_.size());
  for (const auto& t : jobs_)
    result.push_back(t.get());
  return result;
}

std::vector<Breakpoint*> System::GetBreakpoints() const {
  std::vector<Breakpoint*> result;
  result.reserve(breakpoints_.size());
  for (const auto& pair : breakpoints_) {
    if (!pair.second->is_internal())
      result.push_back(pair.second.get());
  }
  return result;
}

std::vector<Filter*> System::GetFilters() const {
  std::vector<Filter*> result;
  result.reserve(filters_.size());
  for (const auto& filter : filters_) {
    result.push_back(filter.get());
  }
  return result;
}

std::vector<SymbolServer*> System::GetSymbolServers() const {
  std::vector<SymbolServer*> result;
  result.reserve(symbol_servers_.size());
  for (const auto& item : symbol_servers_) {
    result.push_back(item.get());
  }
  return result;
}

std::shared_ptr<Download> System::GetDownload(std::string build_id, DebugSymbolFileType file_type,
                                              bool quiet) {
  if (auto existing = downloads_[{build_id, file_type}].lock()) {
    return existing;
  }

  DownloadStarted();

  auto download = std::make_shared<Download>(
      build_id, file_type,
      [build_id, file_type, weak_this = weak_factory_.GetWeakPtr(), quiet](
          const Err& err, const std::string& path) {
        if (!weak_this) {
          return;
        }

        if (!path.empty()) {
          weak_this->download_success_count_++;
          // Adds the file manually since the build_id could already be marked as missing in the
          // build_id_index.
          weak_this->symbols_.build_id_index().AddOneFile(path);

          for (const auto& target : weak_this->targets_) {
            if (auto process = target->process()) {
              process->GetSymbols()->RetryLoadBuildID(build_id, file_type);
            }
          }
        } else {
          weak_this->download_fail_count_++;

          if (!quiet) {
            weak_this->NotifyFailedToFindDebugSymbols(err, build_id, file_type);
          }
        }

        weak_this->DownloadFinished();
      });

  downloads_[{build_id, file_type}] = download;

  if (servers_initializing_) {
    suspended_downloads_.push_back(download);
  }

  return download;
}

void System::DownloadStarted() {
  if (download_count_ == 0) {
    for (auto& observer : session()->download_observers()) {
      observer.OnDownloadsStarted();
    }
  }

  download_count_++;
}

void System::DownloadFinished() {
  download_count_--;

  if (download_count_ == 0) {
    for (auto& observer : session()->download_observers()) {
      observer.OnDownloadsStopped(download_success_count_, download_fail_count_);
    }

    download_success_count_ = download_fail_count_ = 0;
  }
}

void System::RequestDownload(const std::string& build_id, DebugSymbolFileType file_type,
                             bool quiet) {
  auto download = GetDownload(build_id, file_type, quiet);

  for (auto& server : symbol_servers_) {
    if (server->state() != SymbolServer::State::kReady) {
      continue;
    }

    download->AddServer(download, server.get());
  }
}

void System::NotifyFailedToFindDebugSymbols(const Err& err, const std::string& build_id,
                                            DebugSymbolFileType file_type) {
  for (const auto& target : targets_) {
    // Notify only those targets which are processes and which have attempted and failed to load
    // symbols for this build ID previously.
    auto process = target->process();
    if (!process)
      continue;

    for (const auto& status : process->GetSymbols()->GetStatus()) {
      if (status.build_id != build_id) {
        continue;
      }

      if (!err.has_error()) {
        if (file_type == DebugSymbolFileType::kDebugInfo) {
          process->OnSymbolLoadFailure(Err(
              fxl::StringPrintf("Could not load symbols for \"%s\" because there was no mapping "
                                "for build ID \"%s\".",
                                status.name.c_str(), status.build_id.c_str())));
        } else {
          process->OnSymbolLoadFailure(
              Err(fxl::StringPrintf("Could not load binary for \"%s\" because there was no mapping "
                                    "for build ID \"%s\".",
                                    status.name.c_str(), status.build_id.c_str())));
        }
      } else {
        process->OnSymbolLoadFailure(err);
      }
    }
  }
}

void System::OnSymbolServerBecomesReady(SymbolServer* server) {
  for (const auto& target : targets_) {
    auto process = target->process();
    if (!process)
      continue;

    for (const auto& mod : process->GetSymbols()->GetStatus()) {
      if (!mod.symbols || !mod.symbols->module_symbols()) {
        auto download = GetDownload(mod.build_id, DebugSymbolFileType::kDebugInfo, true);
        download->AddServer(download, server);
      } else if (!mod.symbols->module_symbols()->HasBinary()) {
        auto download = GetDownload(mod.build_id, DebugSymbolFileType::kBinary, true);
        download->AddServer(download, server);
      }
    }
  }
}

Process* System::ProcessFromKoid(uint64_t koid) const { return ProcessImplFromKoid(koid); }

void System::GetProcessTree(ProcessTreeCallback callback) {
  session()->remote_api()->ProcessTree(debug_ipc::ProcessTreeRequest(), std::move(callback));
}

Target* System::CreateNewTarget(Target* clone) {
  return CreateNewTargetImpl(static_cast<TargetImpl*>(clone));
}

Job* System::CreateNewJob() {
  auto job = std::make_unique<Job>(session(), false);
  Job* to_return = job.get();
  AddNewJob(std::move(job));
  return to_return;
}

void System::DeleteJob(Job* job) {
  auto found =
      std::find_if(jobs_.begin(), jobs_.end(), [job](auto& cur) { return job == cur.get(); });
  if (found == jobs_.end()) {
    FX_NOTREACHED();  // Should always be found.
    return;
  }

  for (auto& observer : observers_)
    observer.WillDestroyJob(job);

  // Delete all filters that reference this job. While it might be nice if the filter
  // registered for a notification or used a weak pointer for the job, this would imply having a
  // filter enabled/disabled state independent of the other settings which we don't have and don't
  // currently need. Without a disabled state, clearing the job on the filter will make it apply to
  // all jobs which the user does not want.
  std::vector<Filter*> filters_to_remove;
  for (auto& f : filters_) {
    if (f->job() == job)
      filters_to_remove.push_back(f.get());
  }
  for (auto& f : filters_to_remove)
    DeleteFilter(f);

  jobs_.erase(found);
}

Breakpoint* System::CreateNewBreakpoint() {
  auto owning = std::make_unique<BreakpointImpl>(session(), false);
  uint32_t id = owning->backend_id();
  Breakpoint* to_return = owning.get();

  breakpoints_[id] = std::move(owning);

  // Notify observers (may mutate breakpoint list).
  for (auto& observer : observers_)
    observer.DidCreateBreakpoint(to_return);

  return to_return;
}

Breakpoint* System::CreateNewInternalBreakpoint() {
  auto owning = std::make_unique<BreakpointImpl>(session(), true);
  uint32_t id = owning->backend_id();
  Breakpoint* to_return = owning.get();

  breakpoints_[id] = std::move(owning);
  return to_return;
}

void System::DeleteBreakpoint(Breakpoint* breakpoint) {
  BreakpointImpl* impl = static_cast<BreakpointImpl*>(breakpoint);
  auto found = breakpoints_.find(impl->backend_id());
  if (found == breakpoints_.end()) {
    // Should always have found the breakpoint.
    FX_NOTREACHED();
    return;
  }

  // Only notify observers for non-internal breakpoints.
  if (!found->second->is_internal()) {
    for (auto& observer : observers_)
      observer.WillDestroyBreakpoint(breakpoint);
  }
  breakpoints_.erase(found);
}

Filter* System::CreateNewFilter() {
  Filter* to_return = filters_.emplace_back(std::make_unique<Filter>(session())).get();

  // Notify observers (may mutate filter list).
  for (auto& observer : observers_)
    observer.DidCreateFilter(to_return);

  return to_return;
}

void System::DeleteFilter(Filter* filter) {
  auto found = filters_.begin();
  for (; found != filters_.end(); ++found) {
    if (found->get() == filter) {
      break;
    }
  }

  if (found == filters_.end()) {
    // Should always have found the filter.
    FX_NOTREACHED();
    return;
  }

  for (auto& observer : observers_)
    observer.WillDestroyFilter(filter);

  // Move this aside while we modify the list, then let it drop at the end of the function. That way
  // the destructor doesn't see itself in the list of active filters when it emits
  // WillDestroyFilter.
  auto filter_ptr = std::move(*found);
  filters_.erase(found);
}

void System::Pause(fit::callback<void()> on_paused) {
  debug_ipc::PauseRequest request;
  request.process_koid = 0;  // 0 means all processes.
  request.thread_koid = 0;   // 0 means all threads.
  session()->remote_api()->Pause(
      request, [weak_system = weak_factory_.GetWeakPtr(), on_paused = std::move(on_paused)](
                   const Err&, debug_ipc::PauseReply reply) mutable {
        if (weak_system) {
          // Save the newly paused thread metadata. This may need to be
          // generalized if we add other messages that update thread metadata.
          for (const auto& record : reply.threads) {
            if (auto* process = weak_system->ProcessImplFromKoid(record.process_koid)) {
              if (auto* thread = process->GetThreadImplFromKoid(record.thread_koid))
                thread->SetMetadata(record);
            }
          }
        }
        on_paused();
      });
}

void System::Continue(bool forward) {
  // Tell each process to continue as it desires.
  //
  // It would be more efficient to tell the backend to resume all threads in all processes but the
  // Thread client objects have state which needs to be updated (like the current stack) and the
  // thread could have a controller that wants to continue in a specific way (like single-step or
  // step in a range).
  for (const auto& target : targets_) {
    if (Process* process = target->GetProcess())
      process->Continue(forward);
  }
}

bool System::HasDownload(const std::string& build_id) {
  auto download = downloads_.find({build_id, DebugSymbolFileType::kDebugInfo});

  if (download == downloads_.end()) {
    download = downloads_.find({build_id, DebugSymbolFileType::kBinary});
  }

  if (download == downloads_.end()) {
    return false;
  }

  auto ptr = download->second.lock();
  return ptr && ptr->active();
}

std::shared_ptr<Download> System::InjectDownloadForTesting(const std::string& build_id) {
  return GetDownload(build_id, DebugSymbolFileType::kDebugInfo, true);
}

void System::DidConnect() {
  // Force reload the symbol mappings after connection. This needs to be done for every connection
  // since a new image could have been compiled and launched which will have a different build ID
  // file.
  symbols_.build_id_index().ClearCache();

  // Implicitly attach a job to the root. If there was already an implicit job created (from a
  // previous connection) re-use it since there will be settings on it about what processes to
  // attach to that we want to preserve.
  Job* implicit_job = nullptr;
  for (auto& job : jobs_) {
    if (job->is_implicit_root()) {
      implicit_job = job.get();
      break;
    }
  }

  if (!implicit_job) {
    // No previous one, create a new implicit job.
    auto new_job = std::make_unique<Job>(session(), true);
    implicit_job = new_job.get();
    AddNewJob(std::move(new_job));
  }
  implicit_job->AttachToSystemRoot([](fxl::WeakPtr<Job>, const Err&) {});
}

void System::DidDisconnect() {
  for (auto& target : targets_)
    target->ImplicitlyDetach();
  for (auto& job : jobs_)
    job->ImplicitlyDetach();
}

BreakpointImpl* System::BreakpointImplForId(uint32_t id) {
  auto found = breakpoints_.find(id);
  if (found == breakpoints_.end())
    return nullptr;
  return found->second.get();
}

void System::AddNewTarget(std::unique_ptr<TargetImpl> target) {
  Target* for_observers = target.get();

  targets_.push_back(std::move(target));
  for (auto& observer : session()->target_observers())
    observer.DidCreateTarget(for_observers);
}

void System::AddNewJob(std::unique_ptr<Job> job) {
  Job* for_observers = job.get();

  jobs_.push_back(std::move(job));
  for (auto& observer : observers_)
    observer.DidCreateJob(for_observers);
}

void System::OnSettingChanged(const SettingStore& store, const std::string& setting_name) {
  // If any of them change, we have to reinitialize the build_id_index.
  if (setting_name == ClientSettings::System::kSymbolIndexFiles ||
      setting_name == ClientSettings::System::kSymbolPaths ||
      setting_name == ClientSettings::System::kBuildIdDirs ||
      setting_name == ClientSettings::System::kIdsTxts ||
      setting_name == ClientSettings::System::kSymbolCache) {
    // Clear the symbol sources and add them back to sync the index with the setting.
    BuildIDIndex& build_id_index = GetSymbols()->build_id_index();
    build_id_index.ClearAll();

    for (const std::string& path : store.GetList(ClientSettings::System::kSymbolIndexFiles)) {
      build_id_index.AddSymbolIndexFile(path);
    }
    for (const std::string& path : store.GetList(ClientSettings::System::kSymbolPaths)) {
      build_id_index.AddPlainFileOrDir(path);
    }
    for (const std::string& path : store.GetList(ClientSettings::System::kBuildIdDirs)) {
      build_id_index.AddBuildIdDir(path);
    }
    for (const std::string& path : store.GetList(ClientSettings::System::kIdsTxts)) {
      build_id_index.AddIdsTxt(path);
    }

    auto symbol_cache = store.GetString(ClientSettings::System::kSymbolCache);
    if (!symbol_cache.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(std::filesystem::path(symbol_cache), ec);
      build_id_index.AddBuildIdDir(symbol_cache);
    }
  } else if (setting_name == ClientSettings::System::kSymbolServers) {
    // TODO(dangyi): We don't support the removal of an existing symbol server yet.
    auto urls = store.GetList(setting_name);
    std::set<std::string> existing;

    for (const auto& symbol_server : symbol_servers_) {
      existing.insert(symbol_server->name());
    }

    for (const auto& url : urls) {
      if (existing.find(url) == existing.end()) {
        symbol_servers_.push_back(SymbolServer::FromURL(session(), url));
        AddSymbolServer(symbol_servers_.back().get());
      }
    }
  } else if (setting_name == ClientSettings::System::kDebugMode) {
    debug_ipc::SetDebugMode(store.GetBool(setting_name));
  } else {
    FX_LOGS(WARNING) << "Unhandled setting change: " << setting_name;
  }
}

void System::InjectSymbolServerForTesting(std::unique_ptr<SymbolServer> server) {
  symbol_servers_.push_back(std::move(server));
  AddSymbolServer(symbol_servers_.back().get());
}

void System::OnFilterMatches(Job* job, const std::vector<uint64_t>& matched_pids) {
  // Go over the targets and see if we find a valid one for each pid.
  for (uint64_t matched_pid : matched_pids) {
    bool found = false;
    for (auto& target : targets_) {
      Process* process = target->GetProcess();
      if (process && process->GetKoid() == matched_pid) {
        found = true;
        break;
      }
    }

    // If we found an already attached process, we don't care about this match.
    if (found)
      continue;

    AttachToProcess(matched_pid, [matched_pid](fxl::WeakPtr<Target> target, const Err& err) {
      if (err.has_error()) {
        FX_LOGS(ERROR) << "Could not attach to process " << matched_pid;
        return;
      }
    });
  }
}

void System::AttachToProcess(uint64_t pid, Target::Callback callback) {
  // See if there is a target that is not attached.
  Target* open_slot = nullptr;
  for (auto& target : targets_) {
    if (target->GetState() == zxdb::Target::State::kNone) {
      open_slot = target.get();
      break;
    }
  }

  // If no slot was found, we create a new target.
  if (!open_slot)
    open_slot = CreateNewTarget(nullptr);

  open_slot->Attach(pid, std::move(callback));
}

void System::ServerStartedInitializing() { servers_initializing_++; }

void System::ServerFinishedInitializing() {
  FX_DCHECK(servers_initializing_ > 0);

  if (!--servers_initializing_) {
    suspended_downloads_.clear();
  }
}

void System::AddSymbolServer(SymbolServer* server) {
  for (auto& observer : observers_) {
    observer.DidCreateSymbolServer(server);
  }

  bool initializing = false;

  if (server->state() == SymbolServer::State::kInitializing ||
      server->state() == SymbolServer::State::kBusy) {
    initializing = true;
    ServerStartedInitializing();
  }

  server->set_state_change_callback([weak_this = weak_factory_.GetWeakPtr(), initializing](
                                        SymbolServer* server, SymbolServer::State state) mutable {
    if (!weak_this) {
      return;
    }

    if (state == SymbolServer::State::kReady)
      weak_this->OnSymbolServerBecomesReady(server);

    if (initializing && state != SymbolServer::State::kBusy &&
        state != SymbolServer::State::kInitializing) {
      initializing = false;
      weak_this->ServerFinishedInitializing();
    }
  });

  if (server->state() == SymbolServer::State::kReady) {
    OnSymbolServerBecomesReady(server);
  }
}

}  // namespace zxdb
