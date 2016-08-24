#pragma once

#include <runtime/thread.h>

// Initialize the main thread's thread local data. This allocates and
// fills in the thread control block, the tsd, and sets the thread
// pointer.
void __init_tls(mxr_thread_t* thread);
