// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "dnode.h"
#include "devmgr.h"
#include "memfs-private.h"
#include "vfs.h"

#include <magenta/listnode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// create a new dnode and attach it to a vnode
mx_status_t dn_create(dnode_t** out, const char* name, size_t len, vnode_t* vn) {
    mx_status_t status;
    if ((status = dn_allocate(out, name, len)) < 0) {
        return status;
    }
    dn_attach(*out, vn);
    return NO_ERROR;
}

mx_status_t dn_allocate(dnode_t** out, const char* name, size_t len) {
    if ((len > DN_NAME_MAX) || (len < 1)) {
        return ERR_INVALID_ARGS;
    }

    dnode_t* dn;
    if ((dn = calloc(1, sizeof(dnode_t) + len + 1)) == NULL) {
        return ERR_NO_MEMORY;
    }
    dn->flags = len;
    memcpy(dn->name, name, len);
    dn->name[len] = '\0';
    list_initialize(&dn->children);
    *out = dn;
    return NO_ERROR;
}

// Attach a vnode to a dnode
void dn_attach(dnode_t* dn, vnode_t* vn) {
    dn->vnode = vn;
    if (vn != NULL) {
        vn_acquire(vn);
        list_add_tail(&vn->dn_list, &dn->vn_entry);
        vn->dn_count++;
    }
}

void dn_delete(dnode_t* dn) {
    // detach from parent
    if (dn->parent) {
        list_delete(&dn->dn_entry);
        dn->parent = NULL;
    }

    // detach from vnode
    if (dn->vnode) {
        list_delete(&dn->vn_entry);
        dn->vnode->dn_count--;
        vn_release(dn->vnode);
        dn->vnode = NULL;
    }

    free(dn);
}

void dn_add_child(dnode_t* parent, dnode_t* child) {
    if ((parent == NULL) || (child == NULL)) {
        printf("dn_add_child(%p,%p) bad args\n", parent, child);
        panic();
    }
    if (child->parent) {
        printf("dn_add_child: child %p already has parent %p\n", child, parent);
        panic();
    }
    if (child->dn_entry.prev || child->dn_entry.next) {
        printf("dn_add_child: child %p has non-empty dn_entry\n", child);
        panic();
    }

    child->parent = parent;
    list_add_tail(&parent->children, &child->dn_entry);
}

mx_status_t dn_lookup(dnode_t* parent, dnode_t** out, const char* name, size_t len) {
    dnode_t* dn;
    if ((len == 1) && (name[0] == '.')) {
        *out = parent;
        return NO_ERROR;
    }
    if ((len == 2) && (name[0] == '.') && (name[1] == '.')) {
        *out = parent->parent;
        return NO_ERROR;
    }
    list_for_every_entry(&parent->children, dn, dnode_t, dn_entry) {
        if (DN_NAME_LEN(dn->flags) != len) {
            continue;
        }
        if (memcmp(dn->name, name, len) != 0) {
            continue;
        }
        *out = dn;
        return NO_ERROR;
    }
    return ERR_NOT_FOUND;
}

// return the (first) name matching this vnode
mx_status_t dn_lookup_name(const dnode_t* parent, const vnode_t* vn, char* out, size_t out_len) {
    dnode_t* dn;
    list_for_every_entry(&parent->children, dn, dnode_t, dn_entry) {
        if (dn->vnode == vn) {
            mx_off_t len = DN_NAME_LEN(dn->flags);
            if (len > out_len-1) {
                len = out_len-1;
            }
            memcpy(out, dn->name, len);
            out[len] = '\0';
            return NO_ERROR;
        }
    }
    return ERR_NOT_FOUND;
}

// debug printout of file system tree
void dn_print_children(dnode_t* parent, int indent) {
    dnode_t* dn;
    if (indent > 5) return; // error
    list_for_every_entry(&parent->children, dn, dnode_t, dn_entry) {
        printf("%*.s %.*s\n", indent*4, " ", DN_NAME_LEN(dn->flags), dn->name);
        dn_print_children(dn, indent+1);
    }
}

mx_status_t dn_readdir(dnode_t* parent, void* cookie, void* data, size_t len) {
    vdircookie_t* c = cookie;
    dnode_t* last = c->p;
    size_t pos = 0;
    char* ptr = data;
    bool search = (last != NULL);
    mx_status_t r;
    dnode_t* dn;

    list_for_every_entry(&parent->children, dn, dnode_t, dn_entry) {
        if (search) {
            if (dn == last) {
                search = false;
            }
        } else {
            uint32_t vtype = (DN_TYPE(dn->flags) == DN_TYPE_DIR) ? V_TYPE_DIR : V_TYPE_FILE;
            r = vfs_fill_dirent((void*)(ptr + pos), len - pos,
                                dn->name, DN_NAME_LEN(dn->flags),
                                VTYPE_TO_DTYPE(vtype));
            if (r < 0) {
                break;
            }
            last = dn;
            pos += r;
        }
    }
    c->p = last;
    return pos;
}
