// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2009 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

/* command args */
typedef struct {
    const char *str;
    unsigned long u;
    void *p;
    long i;
    bool b;
} cmd_args;

typedef int (*console_cmd)(int argc, const cmd_args *argv, uint32_t flags);

#define CMD_AVAIL_NORMAL (0x1 << 0)
#define CMD_AVAIL_PANIC  (0x1 << 1)
#define CMD_AVAIL_ALWAYS (CMD_AVAIL_NORMAL | CMD_AVAIL_PANIC)

/* command is happening at crash time */
#define CMD_FLAG_PANIC   (0x1 << 0)

/* a block of commands to register */
typedef struct {
    const char *cmd_str;
    const char *help_str;
    const console_cmd cmd_callback;
    uint8_t availability_mask;
} cmd;

/* register a static block of commands at init time */
#if WITH_LIB_CONSOLE

/* enable the panic shell if we're being built */
#if !defined(ENABLE_PANIC_SHELL) && PLATFORM_SUPPORTS_PANIC_SHELL
#define ENABLE_PANIC_SHELL 1
#endif

#define STATIC_COMMAND_START \
    __USED __SECTION(".data.rel.ro.commands") static const cmd _cmd_list[] = {

#define STATIC_COMMAND_END(name) };                                     \

#define STATIC_COMMAND(command_str, help_str, func) { command_str, help_str, func, CMD_AVAIL_NORMAL },
#define STATIC_COMMAND_MASKED(command_str, help_str, func, availability_mask) { command_str, help_str, func, availability_mask },

#else

/* no command blocks, so null them out */
#define STATIC_COMMAND_START
#define STATIC_COMMAND_END(name)
#define STATIC_COMMAND(command_str, help_str, func)

#endif

/* external api */
int console_run_script(const char *string);
int console_run_script_locked(const char *string); // special case from inside a command
console_cmd console_get_command_handler(const char *command);
void console_abort_script(void);

/* panic shell api */
void panic_shell_start(void);

extern int lastresult;

__END_CDECLS
