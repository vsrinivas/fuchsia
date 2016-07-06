// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "vfs.h"
#include "dnode.h"
#include "devmgr.h"

#include <system/listnode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// create a new dnode and attach it to a vnode
mx_status_t dn_create(dnode_t** out, const char* name, size_t len, vnode_t* vn) {
    dnode_t* dn;

    //printf("dn_create: name='%.*s' vn=%p\n", (int)len, name, vn);
    if ((len > DN_NAME_MAX) || (len < 1)) {
        return ERR_INVALID_ARGS;
    }
    if (len > DN_NAME_INLINE) {
        dn = calloc(1, sizeof(dnode_t) + len - DN_NAME_INLINE);
    } else {
        dn = calloc(1, sizeof(dnode_t));
    }
    if (dn == NULL) {
        return ERR_NO_MEMORY;
    }
    dn->name = dn->namedata;
    dn->flags = len;
    dn->refcount = 1;
    dn->vnode = vn;
    if (vn != NULL) {
        vn_acquire(vn);
        list_add_tail(&vn->dn_list, &dn->vn_entry);
        vn->dn_count++;
    }
    list_initialize(&dn->children);
    memcpy(dn->namedata, name, len);
    dn->namedata[len] = 0;
    *out = dn;
    return NO_ERROR;
}

#if 0
void dn_release(dnode_t* dn) {
    if (!(dn->flags & DN_DEAD)) {
        printf("dn_release(%p) not dead!\n", dn);
        panic();
    }
    dn->refcount--;
    if (dn->refcount == 0) {
        dn_delete(dn);
    }
}
#endif

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
            uint32_t vtype = ((dn->flags & DN_TYPE_MASK) == DN_TYPE_DIR) ? V_TYPE_DIR : V_TYPE_FILE;
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
