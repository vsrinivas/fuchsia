#pragma once

#include <magma_common_defs.h>
#include <stddef.h>

namespace magma_enc_util {

size_t size_command_descriptor(magma_command_descriptor* descriptor);
void pack_command_descriptor(void* ptr, magma_connection_t connection, uint32_t context_id, magma_command_descriptor* descriptor);

}
