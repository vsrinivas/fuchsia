" Copyright (c) 2019 The Fuchsia Authors. All rights reserved.
" Use of this source code is governed by a BSD-style license that can be
" found in the LICENSE file.
"
" gotmpl.vim: Vim syntax file Go files that just contain a template
"

syn case match

syn keyword gtAction if else end range template block with define contained
hi def link gtAction Statement

syn region gtSection start="{{" end="}}" contains=gtAction,gtString,gtVariable,gtFunction,gtOperator
hi def link gtSection PreProc

syn keyword gtFunction Eq contained
hi def link gtFunction Operator

syn match gtVariable "\.[a-zA-Z0-9_\.]*" contained
syn match gtVariable "\$[a-zA-Z0-9_\.]*" contained
hi def link gtVariable Identifier

syn region gtString start="\"" end="\"" contained
hi def link gtString String

syn match gtOperator /:=/ contained
syn match gtOperator /,/ contained
hi def link gtOperator Operator
