" Vim syntax file " Language: Fidl
" To get syntax highlighting for .fidl files, add the following to your .vimrc
" file:
"     source /path/to/src/tools/vim/fidl.vim
if !exists("g:main_syntax")
  if version < 600
    syntax clear
  elseif exists("b:current_syntax")
    finish
  endif
  let g:main_syntax = 'fidl'
  syntax region fidlFold start="{" end="}" transparent fold
endif

" keyword definitions
syn match fidlImport        "^\(import\)\s"
syn keyword fidlLanguageKeywords enum module interface struct union
syn keyword fidlType array bool float double int8 int16 int32 int64 uint8 uint16 uint32 uint64 string handle channel socket vmo process job thread

" Comments
syntax keyword fidlTodo           contained TODO FIXME XXX
syntax region  fidlDocLink        contained start=+\[+ end=+\]+
syntax region  fidlComment        start="/\*"  end="\*/" contains=fidlTodo,fidlDocLink,@Spell
syntax match   fidlLineComment    "//.*" contains=fidlTodo,@Spell
syntax match   fidlLineDocComment "///.*" contains=fidlTodo,fidlDocLink,@Spell

syn region fidlString start='"' end='"' contained
syn region fidlDesc start='\[' end='\]'

let b:current_syntax = "fidl"
let b:spell_options = "contained"

hi def link fidlImport      Include
hi def link fidlLanguageKeywords Keyword
hi def link fidlTodo        Todo

hi def link fidlComment         Comment
hi def link fidlLineComment     Comment
hi def link fidlLineDocComment  Comment
hi def link fidlDocLink         SpecialComment

hi def link fidlBlockCmd    Statement
hi def link fidlType        Type
hi def link fidlString      Constant
hi def link fidlDesc        PreProc
hi def link fidlNumber      Constanterface

if g:main_syntax is# 'fidl'
  unlet g:main_syntax
endif
