// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <linenoise/linenoise.h>
#include <magenta/assert.h>

#include "shell.h"
#include "nodes.h"

#include "exec.h"
#include "memalloc.h"
#include "var.h"

typedef struct {
    // An index into the tokenized string which points at the first
    // character of the last token (ie space separated component) of
    // the line.
    size_t start;
    // Whether there are multiple non-enviroment components of the
    // line to tokenize. For example:
    //     foo          # found_command = false;
    //     foo bar      # found_command = true;
    //     FOO=BAR quux # found_command = false;
    bool found_command;
    // Whether the end of the line is in a space-free string of the
    // form 'FOO=BAR', which is the syntax to set an environment
    // variable.
    bool in_env;
} token_t;

static token_t tokenize(const char* line, size_t line_length) {
    token_t token = {
        .start = 0u,
        .found_command = false,
        .in_env = false,
    };
    bool in_token = false;

    for (size_t i = 0; i < line_length; i++) {
        if (line[i] == ' ') {
            token.start = i + 1;

            if (in_token && !token.in_env) {
                token.found_command = true;
            }

            in_token = false;
            token.in_env = false;
            continue;
        }

        in_token = true;
        token.in_env = token.in_env || line[i] == '=';
    }

    return token;
}

typedef struct {
    const char* line_prefix;
    const char* line_separator;
    const char* file_prefix;
} completion_state_t;

// Generate file name completions. |dir| is the directory to for
// matching filenames. File names must match |state->file_prefix| in
// order to be entered into |completions|. |state->line_prefix| and
// |state->line_separator| begin the line before the file completion.
static void complete_at_dir(DIR* dir, completion_state_t* state,
                            linenoiseCompletions* completions) {
    MX_DEBUG_ASSERT(strchr(state->file_prefix, '/') == NULL);
    size_t file_prefix_len = strlen(state->file_prefix);

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(state->file_prefix, de->d_name, file_prefix_len)) {
            continue;
        }
        if (!strcmp(de->d_name, ".")) {
            continue;
        }
        if (!strcmp(de->d_name, "..")) {
            continue;
        }

        char completion[LINE_MAX];
        strncpy(completion, state->line_prefix, sizeof(completion));
        completion[sizeof(completion) - 1] = '\0';
        size_t remaining = sizeof(completion) - strlen(completion) - 1;
        strncat(completion, state->line_separator, remaining);
        remaining = sizeof(completion) - strlen(completion) - 1;
        strncat(completion, de->d_name, remaining);

        linenoiseAddCompletion(completions, completion);
    }
}

void tab_complete(const char* line, linenoiseCompletions* completions) {
    size_t input_line_length = strlen(line);

    token_t token = tokenize(line, input_line_length);

    if (token.in_env) {
        // We can't tab complete environment variables.
        return;
    }

    char buf[LINE_MAX];
    size_t token_length = input_line_length - token.start;
    if (token_length >= sizeof(buf)) {
        return;
    }
    strncpy(buf, line, sizeof(buf));
    char* partial_path = buf + token.start;

    // The following variables are set by the following block of code
    // in each of three different cases:
    //
    // 1. There is no slash in the last token, and we are giving an
    //    argument to a command. An example:
    //        foo bar ba
    //    We are searching the current directory (".") for files
    //    matching the prefix "ba", to join with a space to the line
    //    prefix "foo bar".
    //
    // 2. There is no slash in the only token. An example:
    //        fo
    //    We are searching the PATH environment variable for files
    //    matching the prefix "fo". There is no line prefix or
    //    separator in this case.
    //
    // 3. There is a slash in the last token. An example:
    //        foo bar baz/quu
    //    In this case, we are searching the directory specified by
    //    the token (up until the final '/', so "baz" in this case)
    //    for files with the prefix "quu", to join with a slash to the
    //    line prefix "foo bar baz".
    completion_state_t completion_state;
    const char** paths = NULL;

    // |paths| for cases 1 and 3 respectively.
    const char* local_paths[] = { ".", NULL };
    const char* partial_paths[] = { partial_path, NULL };

    char* file_prefix = strrchr(partial_path, '/');
    if (file_prefix == NULL) {
        file_prefix = partial_path;
        if (token.found_command) {
            // Case 1.
            // Because we are in a command, partial_path[-1] is a
            // space we want to zero out.
            MX_DEBUG_ASSERT(token.start > 0);
            MX_DEBUG_ASSERT(partial_path[-1] == ' ');
            partial_path[-1] = '\0';

            completion_state.line_prefix = buf;
            completion_state.line_separator = " ";
            completion_state.file_prefix = file_prefix;
            paths = local_paths;
        } else {
            // Case 2.
            completion_state.line_prefix = "";
            completion_state.line_separator = "";
            completion_state.file_prefix = file_prefix;
        }
    } else {
        // Case 3.
        // Because we are in a multiple component file path,
        // *file_prefix is a '/' we want to zero out.
        MX_DEBUG_ASSERT(*file_prefix == '/');
        *file_prefix = '\0';

        completion_state.line_prefix = buf;
        completion_state.line_separator = "/";
        completion_state.file_prefix = file_prefix + 1;
        paths = partial_paths;

        // If the partial path is empty, it means we were given
        // something like "/foo". We should therefore set the path to
        // search to "/".
        if (strlen(paths[0]) == 0) {
            paths[0] = "/";
        }
    }

    if (paths) {
        for (; *paths != NULL; paths++) {
            DIR* dir = opendir(*paths);
            if (dir == NULL) {
                continue;
            }
            complete_at_dir(dir, &completion_state, completions);
            closedir(dir);
        }
    } else {
        const char* path_env = pathval();
        char* pathname;
        while ((pathname = padvance(&path_env, "")) != NULL) {
            DIR* dir = opendir(pathname);
            stunalloc(pathname);
            if (dir == NULL) {
                continue;
            }
            complete_at_dir(dir, &completion_state, completions);
            closedir(dir);
        }
    }
}

#ifdef mkinit
INCLUDE "tab.h"
INCLUDE <linenoise/linenoise.h>
INIT {
    linenoiseSetCompletionCallback(tab_complete);
}
#endif
