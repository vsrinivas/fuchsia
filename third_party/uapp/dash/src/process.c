// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <string.h>
#include <unistd.h>

#include "shell.h"
#include "memalloc.h"
#include "nodes.h"
#include "exec.h"
#include "process.h"
#include "options.h"
#include "var.h"

static void prepare_launch(launchpad_t* lp, const char* filename, int argc,
                           const char* const* argv, const char* const* envp,
                           int *fds) {

    launchpad_load_from_file(lp, filename);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);
    launchpad_clone(lp, LP_CLONE_FDIO_NAMESPACE | LP_CLONE_FDIO_CWD);

    if (fds) {
        launchpad_clone_fd(lp, fds[0], STDIN_FILENO);
        launchpad_clone_fd(lp, fds[1], STDOUT_FILENO);
        launchpad_clone_fd(lp, fds[2], STDERR_FILENO);
    } else {
        launchpad_clone_fd(lp, STDIN_FILENO, STDIN_FILENO);
        launchpad_clone_fd(lp, STDOUT_FILENO, STDOUT_FILENO);
        launchpad_clone_fd(lp, STDERR_FILENO, STDERR_FILENO);
    }
}

static zx_status_t launch(const char* filename, int argc, const char* const* argv,
                          const char* const* envp, zx_handle_t* process, const char** errmsg) {
    launchpad_t* lp = NULL;
    launchpad_create(0, filename, &lp);
    prepare_launch(lp, filename, argc, argv, envp, NULL);
    return launchpad_go(lp, process, errmsg);
}

// Add all function definitions to our nodelist, so we can package them up for a
// subshell.
static void
addfuncdef(struct cmdentry *entry, void *token)
{
    if (entry->cmdtype == CMDFUNCTION) {
        struct nodelist **cmdlist = (struct nodelist **) token;
        struct nodelist *newnode = ckmalloc(sizeof(struct nodelist));
        newnode->next = *cmdlist;
        newnode->n = &entry->u.func->n;
        *cmdlist = newnode;
    }
}

zx_status_t process_subshell(union node* n, const char* const* envp, zx_handle_t* process, int *fds,
                             const char** errmsg)
{
    if (!orig_arg0)
        return ZX_ERR_NOT_FOUND;

    launchpad_t* lp = NULL;

    // TODO(abarth): Handle the redirects properly (i.e., implement
    // redirect(n->nredir.redirect) using launchpad);
    zx_handle_t ast_vmo = ZX_HANDLE_INVALID;

    // Create a node for our expression
    struct nodelist *nlist = ckmalloc(sizeof(struct nodelist));
    nlist->n = n;
    nlist->next = NULL;

    // Create nodes for all function definitions
    hashiter(addfuncdef, &nlist);

    // Encode the node list
    zx_status_t status = codec_encode(nlist, &ast_vmo);

    // Clean up
    while (nlist) {
        struct nodelist *next = nlist->next;
        ckfree(nlist);
        nlist = next;
    }

    if (status != ZX_OK)
        return status;

    launchpad_create(0, orig_arg0, &lp);

    // Construct an argv array
    int argc = 1 + shellparam.nparam;
    const char *argv[argc];
    argv[0] = orig_arg0;
    size_t arg_ndx;
    for (arg_ndx = 0; arg_ndx < shellparam.nparam; arg_ndx++) {
        argv[arg_ndx + 1] = shellparam.p[arg_ndx];
    }

    prepare_launch(lp, orig_arg0, argc, (const char* const*)argv, envp, fds);
    launchpad_add_handle(lp, ast_vmo, PA_HND(PA_USER0, 0));
    return launchpad_go(lp, process, errmsg);
}

int process_launch(int argc, const char* const* argv, const char* path, int index, zx_handle_t* process,
                   zx_status_t* status_out, const char** errmsg) {
    zx_status_t status = ZX_OK;

    // All exported variables
    const char* const* envp = (const char* const*)environment();

    if (strchr(argv[0], '/') != NULL) {
        status = launch(argv[0], argc, argv, envp, process, errmsg);
    } else {
        status = ZX_ERR_NOT_FOUND;
        const char* filename = NULL;
        while (status != ZX_OK && (filename = padvance(&path, argv[0])) != NULL) {
            if (--index < 0 && pathopt == NULL)
                status = launch(filename, argc, argv, envp, process, errmsg);
            stunalloc(filename);
        }
    }

    *status_out = status;

    switch (status) {
    case ZX_OK:
        return 0;
    case ZX_ERR_ACCESS_DENIED:
        return 126;
    case ZX_ERR_NOT_FOUND:
        return 127;
    default:
        return 2;
    }
}

/* Check for process termination (block if requested). When not blocking,
   returns ZX_ERR_TIMED_OUT if process hasn't exited yet.  */
int process_await_termination(zx_handle_t process, bool blocking) {
    zx_time_t timeout = blocking ? ZX_TIME_INFINITE : 0;
    zx_signals_t signals_observed;
    zx_status_t status = zx_object_wait_one(process, ZX_TASK_TERMINATED, timeout, &signals_observed);
    if (status != ZX_OK && status != ZX_ERR_TIMED_OUT)
        return status;
    if (!blocking && status == ZX_ERR_TIMED_OUT && !signals_observed)
        return ZX_ERR_TIMED_OUT;

    zx_info_process_t proc_info;
    status = zx_object_get_info(process, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), NULL, NULL);
    if (status != ZX_OK)
        return status;

    return proc_info.return_code;
}
