#ifndef TOOLS_FIDLCAT_LIB_CODE_GENERATOR_CODE_GENERATOR_H_
#define TOOLS_FIDLCAT_LIB_CODE_GENERATOR_CODE_GENERATOR_H_

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>

#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/printer.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

class CodeGenerator {
 public:
  CodeGenerator() {}
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_CODE_GENERATOR_CODE_GENERATOR_H_
