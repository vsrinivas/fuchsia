// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decoder.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "garnet/lib/debugger_utils/util.h"

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace intel_processor_trace {

static void* MmapFile(const char* file, size_t* size) {
  int fd = open(file, O_RDONLY);
  if (fd < 0)
    return nullptr;
  struct stat st;
  void* map = MAP_FAILED;
  if (fstat(fd, &st) >= 0) {
    *size = st.st_size;
    map = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  }
  close(fd);
  return map != MAP_FAILED ? map : nullptr;
}

static void UnmapFile(void* map, size_t size) { munmap(map, size); }

std::unique_ptr<DecoderState> DecoderState::Create(
    const DecoderConfig& config) {
  auto decoder = std::unique_ptr<DecoderState>(new DecoderState());

  FXL_DCHECK(config.pt_file_name != "" || config.pt_list_file_name != "");
  FXL_DCHECK(config.ktrace_file_name != "");

  if (!decoder->AllocImage("ipt-dump"))
    return nullptr;

  // Read sideband data before we read anything else.

  if (!decoder->ReadKtraceFile(config.ktrace_file_name))
    return nullptr;

  for (auto& f : config.map_file_names) {
    if (!decoder->ReadMapFile(f))
      return nullptr;
  }

  for (auto& f : config.ids_file_names) {
    if (!decoder->ReadIdsFile(f))
      return nullptr;
  }

  if (config.pt_file_name != "") {
    decoder->AddPtFile(files::GetCurrentDirectory(), PtFile::kIdUnset,
                       config.pt_file_name);
  } else {
    if (!decoder->ReadPtListFile(config.pt_list_file_name))
      return nullptr;
  }

  for (auto& f : config.elf_file_names) {
    // TODO(dje): This isn't useful without base addr, cr3, etc.
    if (!decoder->ReadElf(f, 0, 0, 0, 0))
      return nullptr;
  }

  if (config.kernel_file_name != "") {
    decoder->SetKernelCr3(config.kernel_cr3);
    if (!decoder->ReadKernelElf(config.kernel_file_name, config.kernel_cr3))
      return nullptr;
  }

  return decoder;
}

DecoderState::DecoderState()
    : image_(nullptr), decoder_(nullptr), kernel_cr3_(pt_asid_no_cr3) {
  pt_config_init(&config_);
}

DecoderState::~DecoderState() {
  if (config_.begin)
    UnmapFile(config_.begin, config_.end - config_.begin);
  if (decoder_)
    pt_insn_free_decoder(decoder_);
  if (image_)
    pt_image_free(image_);
}

Process::Process(zx_koid_t p, uint64_t c, uint64_t start, uint64_t end)
    : pid(p), cr3(c), start_time(start), end_time(end) {
  FXL_VLOG(2) << fxl::StringPrintf(
      "pid %" PRIu64 " cr3 0x%" PRIx64 " start %" PRIu64, pid, cr3, start_time);
}

PtFile::PtFile(uint64_t i, const std::string& f) : id(i), file(f) {
  FXL_VLOG(2) << fxl::StringPrintf("pt_file %" PRIu64 ", file %s", id,
                                   file.c_str());
}

const Process* DecoderState::LookupProcessByPid(zx_koid_t pid) {
  // TODO(dje): Add O(1) lookup when there's a need.
  for (const auto& p : processes_) {
    if (p.pid == pid)
      return &p;
  }

  return nullptr;
}

const Process* DecoderState::LookupProcessByCr3(uint64_t cr3) {
  // TODO(dje): Add O(1) lookup when there's a need.
  for (const auto& p : processes_) {
    if (p.cr3 == cr3)
      return &p;
    // If tracing just threads, cr3 values in the trace may be this.
    // If there's only one process, we're ok.
    // TODO(dje): Tracing threads with multiple processes.
    if (cr3 == pt_asid_no_cr3 && processes_.size() == 1)
      return &p;
  }

  return nullptr;
}

const LoadMap* DecoderState::LookupMapEntry(zx_koid_t pid, uint64_t addr) {
  return load_maps_.LookupLoadMap(pid, addr);
}

const BuildId* DecoderState::LookupBuildId(const std::string& bid) {
  return build_ids_.LookupBuildId(bid);
}

std::string DecoderState::LookupFile(const std::string& file) {
  // TODO(dje): This function is here in case we need to do fancier lookup
  // later.
  return file;
}

// static
int DecoderState::ReadMemCallback(uint8_t* buffer, size_t size,
                                  const struct pt_asid* asid, uint64_t addr,
                                  void* context) {
  auto decoder = reinterpret_cast<DecoderState*>(context);
  uint64_t cr3 = asid->cr3;

  auto proc = decoder->LookupProcessByCr3(cr3);
  if (!proc) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "process lookup failed for cr3:"
        " 0x%" PRIx64,
        cr3);
    decoder->unknown_cr3s_.emplace(cr3);
    return -pte_nomap;
  }

  auto map = decoder->LookupMapEntry(proc->pid, addr);
  if (!map) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "map lookup failed for cr3/addr:"
        " 0x%" PRIx64 "/0x%" PRIx64,
        cr3, addr);
    return -pte_nomap;
  }

  auto bid = decoder->LookupBuildId(map->build_id);
  if (!bid) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "build_id not found: %s, for cr3/addr:"
        " 0x%" PRIx64 "/0x%" PRIx64,
        map->build_id.c_str(), cr3, addr);
    return -pte_nomap;
  }

  auto file = decoder->LookupFile(bid->file);
  if (!file.size()) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "file not found: %s, for build_id %s, cr3/addr:"
        " 0x%" PRIx64 "/0x%" PRIx64,
        bid->file.c_str(), map->build_id.c_str(), cr3, addr);
    return -pte_nomap;
  }

  if (!decoder->ReadElf(file.c_str(), map->base_addr, cr3, 0,
                        map->end_addr - map->load_addr)) {
    FXL_VLOG(1) << "Reading ELF file failed: " << file;
    return -pte_nomap;
  }

  return pt_image_read_for_callback(decoder->image_, buffer, size, asid, addr);
}

bool DecoderState::AllocImage(const std::string& name) {
  FXL_DCHECK(!image_);

  struct pt_image* image = pt_image_alloc(name.c_str());
  FXL_DCHECK(image);

  pt_image_set_callback(image, ReadMemCallback, this);

  image_ = image;

  return true;
}

bool DecoderState::AddProcess(zx_koid_t pid, uint64_t cr3,
                              uint64_t start_time) {
  FXL_VLOG(2) << fxl::StringPrintf("New process: %" PRIu64 ", cr3 0x%" PRIx64
                                   " @%" PRIu64,
                                   pid, cr3, start_time);
  processes_.push_back(Process(pid, cr3, start_time, 0));
  return true;
}

bool DecoderState::MarkProcessExited(zx_koid_t pid, uint64_t end_time) {
  FXL_VLOG(2) << fxl::StringPrintf(
      "Marking process exit: %" PRIu64 " @%" PRIu64, pid, end_time);

  // We don't remove the process as process start/exit records are read in
  // one pass over the ktrace file. Instead just mark when it exited.
  // We assume process ids won't wrap, which is pretty safe for now.
  for (auto i = processes_.begin(); i != processes_.end(); ++i) {
    if (i->pid == pid) {
      i->end_time = end_time;
      break;
    }
  }

  // If we didn't find an entry that's ok. We might have gotten a process-exit
  // notification for a process that we didn't get a start notification for.
  return true;
}

void DecoderState::AddPtFile(const std::string& file_dir, uint64_t id,
                             const std::string& path) {
  std::string abs_path;

  // Convert relative paths to absolute ones.
  if (path[0] != '/') {
    std::string abs_file_dir = files::AbsolutePath(file_dir);
    abs_path = abs_file_dir + "/" + path;
  } else {
    abs_path = path;
  }
  pt_files_.push_back(PtFile(id, abs_path));
}

bool DecoderState::AllocDecoder(const std::string& pt_file_name) {
  unsigned zero = 0;

  FXL_DCHECK(decoder_ == nullptr);

  pt_cpu_errata(&config_.errata, &config_.cpu);
  // When no bit is set, set all, as libipt does not keep up with newer
  // CPUs otherwise.
  if (!memcmp(&config_.errata, &zero, 4))
    memset(&config_.errata, 0xff, sizeof(config_.errata));

  size_t len;
  unsigned char* map =
      reinterpret_cast<unsigned char*>(MmapFile(pt_file_name.c_str(), &len));
  if (!map) {
    fprintf(stderr, "Cannot open PT file %s: %s\n", pt_file_name.c_str(),
            strerror(errno));
    return false;
  }
  config_.begin = map;
  config_.end = map + len;

  decoder_ = pt_insn_alloc_decoder(&config_);
  if (!decoder_) {
    fprintf(stderr, "Cannot create PT decoder\n");
    UnmapFile(map, len);
    return false;
  }

  pt_insn_set_image(decoder_, image_);

  return true;
}

void DecoderState::FreeDecoder() {
  FXL_DCHECK(decoder_);
  pt_insn_free_decoder(decoder_);
  decoder_ = nullptr;
}

const SymbolTable* DecoderState::FindSymbolTable(uint64_t cr3, uint64_t pc) {
  return simple_pt::FindSymbolTable(symtabs_, cr3, pc);
}

const Symbol* DecoderState::FindSymbol(uint64_t cr3, uint64_t pc,
                                       const SymbolTable** out_symtab) {
  return simple_pt::FindSymbol(symtabs_, cr3, pc, out_symtab);
}

const char* DecoderState::FindPcFileName(uint64_t cr3, uint64_t pc) {
  return simple_pt::FindPcFileName(symtabs_, cr3, pc);
}

bool DecoderState::SeenCr3(uint64_t cr3) {
  return simple_pt::SeenCr3(symtabs_, cr3);
}

}  // namespace intel_processor_trace
