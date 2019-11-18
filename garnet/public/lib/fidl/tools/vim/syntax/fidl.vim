" Vim syntax file " Language: Fidl
" To get syntax highlighting for .fidl files, add the following to your .vimrc
" file:
"     source /path/to/src/tools/vim/fidl.vim
"
" Based on the grammar describe in
"
"     https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/grammar.md
"
" There is still some work to be done.
if !exists("g:main_syntax")
  if version < 600
    syntax clear
  elseif exists("b:current_syntax")
    finish
  endif
  let g:main_syntax = 'fidl'
  syntax region fidlFold start="{" end="}" transparent fold
endif

" Keywords
syn keyword fidlKeyword as bits compose const enum error library protocol struct table union using xunion

" Types
syn match fidlType "\<request<@\?[a-zA-Z][a-zA-Z0-9]*\(\.[a-zA-Z][a-zA-Z0-9]*\)*>?\?"

syn match fidlType "\<handle\>?\?"
" Exhaustive list of the handle types is listed below.  Highlight anything else
" as invalid.
syn match fidlBadType "\<handle<[^>]*>?\?"
syn match fidlType "\<handle<\(channel\|debuglog\|event\|eventpair\|fifo\|guest\|interrupt\|iommu\|job\|pager\|pcidevice\|pmt\|port\|process\|profile\|resource\|socket\|suspendtoken\|thread\|timer\|vcpu\|vmar\|vmo\)>?\?"

syn match fidlType "\<string\>\%(:\%(\d\+\|\K\k*\%(\.\K\k*\)*\)\)\??\?"
syn match fidlType "\<bool\>"
syn match fidlBadType "\<bool?"
syn match fidlType "\<float\(32\|64\)\>"
syn match fidlBadType "\<float\(32\|64\)?"
syn match fidlType "\<u\?int\(8\|16\|32\|64\)\>"
syn match fidlBadType "\<u\?int\(8\|16\|32\|64\)?"

syn region fidlType matchgroup=Type start="\<vector<" end=">\%(:\%(\d\+\|\K\k*\%(\.\K\k*\)*\)\)\??\?" contains=fidlType,fidlBadType transparent
syn region fidlType matchgroup=Type start="\<array<" end=">:\%(\d\+\|\K\k*\%(\.\K\k*\)*\)" contains=fidlType,fidlBadType transparent


" Identifiers prefixed with @
syn match fidlEscapedIdentifier "@[a-zA-Z][a-zA-Z0-9]*"


" Comments
syntax keyword fidlTodo           contained TODO FIXME XXX
syntax region  fidlDocLink        contained start=+\[+ end=+\]+
syntax match   fidlLineComment    "//.*" contains=fidlTodo,@Spell
syntax match   fidlLineDocComment "///.*" contains=fidlTodo,fidlDocLink,@Spell


" Literals
syn region fidlString start='"' end='"' contained


" Attributes
syn region fidlAttribute start='\[' end='\]'

let b:current_syntax = "fidl"
let b:spell_options = "contained"

hi def link fidlKeyword Keyword

hi def link fidlTodo            Todo
hi def link fidlLineComment     Comment
hi def link fidlLineDocComment  SpecialComment
hi def link fidlDocLink         SpecialComment

hi def link fidlBlockCmd    Statement
hi def link fidlType        Type
hi def link fidlBadType     Error
hi def link fidlIntType     Type
hi def link fidlString      String
hi def link fidlAttribute   PreProc
hi def link fidlNumber      Number

hi def link fidlEscapedIdentifier Normal

if g:main_syntax is# 'fidl'
  unlet g:main_syntax
endif
