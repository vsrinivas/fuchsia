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
" 
" Start from:
"   sed '/## Grammar/,$!d' docs/reference/fidl/language/grammar.md |
"   sed '/-------/,$d' |
"   grep -P '"[a-z]*?"'
" to update.
syn keyword fidlKeyword alias as bits compose const enum error false flexible library properties protocol reserved resource resource_definition service strict struct table true type union using

" Types
syn match fidlType "\<request<@\?[a-zA-Z][a-zA-Z0-9]*\(\.[a-zA-Z][a-zA-Z0-9]*\)*>?\?"

" From zircon/vdso/zx_common.fidl:
syn match fidlBadType "\<zx.handle:[A-Z]*?\?"
" The above flags zx.handle:TYPO, but then reallow zx.handle:< anything > for
" complex handle types.
syn match fidlType "\<zx.handle:<[^>]*>\?"
" And specifically make known handle subtypes recognized.
syn match fidlType "\<zx.handle:\(BTI\|CHANNEL\|CLOCK\|EVENT\|EVENTPAIR\|EXCEPTION\|FIFO\|GUEST\|INTERRUPT\|IOMMU\|JOB\|LOG\|MSI\|PAGER\|PCI_DEVICE\|PMT\|PORT\|PROCESS\|PROFILE\|RESOURCE\|SOCKET\|STREAM\|SUSPEND_TOKEN\|THREAD\|TIMER\|VCPU\|VMAR\|VMO\)\>"

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
