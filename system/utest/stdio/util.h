#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// readable: is the pipe readable on the child side?
// returns [our_fd, child_fd]
int stdio_pipe(int pipe_fds[2], bool readable);

int read_to_end(int fd, uint8_t** buf, size_t* buf_size);
