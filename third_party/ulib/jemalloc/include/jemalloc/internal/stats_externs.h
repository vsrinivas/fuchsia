#ifndef JEMALLOC_INTERNAL_STATS_EXTERNS_H
#define JEMALLOC_INTERNAL_STATS_EXTERNS_H

#pragma GCC visibility push(hidden)

extern bool	opt_stats_print;

void	stats_print(void (*write)(void *, const char *), void *cbopaque,
    const char *opts);

#pragma GCC visibility pop

#endif /* JEMALLOC_INTERNAL_STATS_EXTERNS_H */
