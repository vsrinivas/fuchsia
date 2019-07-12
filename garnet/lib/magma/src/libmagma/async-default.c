#include <lib/async/default.h>

// Gets the current thread's default asynchronous dispatcher interface.
// Returns |NULL| if none.
// __attribute__ ((__visibility__("default"))) async_dispatcher_t*
// async_get_default_dispatcher(void)
// {
//     return NULL;
// }

// // Sets the current thread's default asynchronous dispatcher interface.
// // May be set to |NULL| if this thread doesn't have a default dispatcher.
// __attribute__ ((__visibility__("default"))) void async_set_default_dispatcher(async_dispatcher_t*
// dispatcher)
// {}
