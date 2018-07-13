// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "garnet/lib/debugger_utils/build_ids.h"
#include "garnet/lib/debugger_utils/elf_reader.h"
#include "garnet/lib/debugger_utils/elf_symtab.h"
#include "garnet/lib/debugger_utils/ktrace_reader.h"
#include "garnet/lib/debugger_utils/load_maps.h"

#include "third_party/processor-trace/libipt/include/intel-pt.h"

#include "third_party/simple-pt/symtab.h"

namespace intel_processor_trace {

// Parameters needed to drive the decoder.

struct DecoderConfig {
  // Path to the kernel ELF file.
  std::string kernel_file_name;

  // Ideally this should come from sideband data, but it can be manually
  // specified.
  uint64_t kernel_cr3 = pt_asid_no_cr3;

  // Path to the raw processor trace dump.
  // This file is produced by the "ipt" program.
  std::string pt_file_name;

  // Path to a text file containing a mapping of "id" values and their
  // corresponding PT dump files.
  // The format of each line is "id /path/to/pt-file".
  // "id" is either a cpu number for cpu-based tracing, or thread id for
  // thread-based tracing; in decimal.
  std::string pt_list_file_name;

  // Path to needed ktrace data.
  // This file is produced by the "ipt" program.
  std::string ktrace_file_name;

  // Optional additional files passed on the command line.
  std::vector<std::string> elf_file_names;

  // Path to the "ids.txt" files from the build.
  std::vector<std::string> ids_file_names;

  // Path to file containing linker map output.
  std::vector<std::string> map_file_names;
};

struct Process {
  Process(zx_koid_t p, uint64_t c, uint64_t start, uint64_t end);
  zx_koid_t pid;
  uint64_t cr3;
  // The time, in units ktrace uses, when the process was live.
  // An end time of zero means "unknown".
  uint64_t start_time, end_time;
};

struct PtFile {
  PtFile(uint64_t i, const std::string& f);

  // The id of the cpu we are processing. Cpus are numbered 0...N.
  static constexpr uint64_t kIdUnset = ~(uint64_t)0;

  uint64_t id;
  std::string file;
};

using SymbolTable = simple_pt::SymbolTable;
using Symbol = simple_pt::Symbol;
using LoadMap = debugger_utils::LoadMap;
using BuildId = debugger_utils::BuildId;

class DecoderState {
 public:
  static std::unique_ptr<DecoderState> Create(const DecoderConfig& config);

  ~DecoderState();

  const Process* LookupProcessByPid(zx_koid_t pid);
  const Process* LookupProcessByCr3(uint64_t cr3);

  const LoadMap* LookupMapEntry(zx_koid_t pid, uint64_t addr);

  const BuildId* LookupBuildId(const std::string& bid);

  std::string LookupFile(const std::string& file);

  const SymbolTable* FindSymbolTable(uint64_t cr3, uint64_t pc);
  const Symbol* FindSymbol(uint64_t cr3, uint64_t pc,
                           const SymbolTable** out_symtab);
  const char* FindPcFileName(uint64_t cr3, uint64_t pc);

  bool SeenCr3(uint64_t cr3);

  uint64_t kernel_cr3() const { return kernel_cr3_; }
  void set_kernel_cr3(uint64_t cr3) { kernel_cr3_ = cr3; }

  const pt_config& config() const { return config_; }
  void set_nom_freq(uint8_t nom_freq) { config_.nom_freq = nom_freq; }
  void set_family(uint32_t family) { config_.cpu.family = family; }
  void set_model(uint32_t model) { config_.cpu.model = model; }
  void set_stepping(uint32_t stepping) { config_.cpu.stepping = stepping; }

  bool AllocDecoder(const std::string& pt_file);
  void FreeDecoder();
  pt_insn_decoder* decoder() const { return decoder_; }

  const std::vector<Process>& processes() const { return processes_; }

  const std::vector<PtFile>& pt_files() const { return pt_files_; }

  const std::unordered_set<uint64_t>& unknown_cr3s() const {
    return unknown_cr3s_;
  }

 private:
  DecoderState();

  bool AllocImage(const std::string& name);

  bool ReadKtraceFile(const std::string& file);
  bool ReadMapFile(const std::string& file);
  bool ReadIdsFile(const std::string& file);
  bool ReadPtListFile(const std::string& file);

  void AddSymtab(std::unique_ptr<SymbolTable> symtab);
  bool ReadElf(const std::string& file, uint64_t base, uint64_t cr3,
               uint64_t file_off, uint64_t map_len);
  bool ReadKernelElf(const std::string& file, uint64_t cr3);

  void SetKernelCr3(uint64_t cr3) { kernel_cr3_ = cr3; }

  static int ReadMemCallback(uint8_t* buffer, size_t size,
                             const struct pt_asid* asid, uint64_t addr,
                             void* context);

  static int ProcessKtraceRecord(debugger_utils::KtraceRecord* rec,
                                 void* arg);

  bool AddProcess(zx_koid_t pid, uint64_t cr3, uint64_t start_time);
  bool MarkProcessExited(zx_koid_t pid, uint64_t end_time);

  void AddPtFile(const std::string& file_dir, uint64_t id,
                 const std::string& path);

  pt_config config_;
  pt_image* image_;
  pt_insn_decoder* decoder_;

  uint64_t kernel_cr3_;

  std::vector<Process> processes_;
  debugger_utils::LoadMapTable load_maps_;
  debugger_utils::BuildIdTable build_ids_;
  std::vector<PtFile> pt_files_;

  // List of cr3 values seen that we don't have processes for.
  // This helps printers explain the results to human readers.
  std::unordered_set<uint64_t> unknown_cr3s_;

  std::vector<std::unique_ptr<SymbolTable>> symtabs_;
};

}  // namespace intel_processor_trace
