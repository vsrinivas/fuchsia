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

// For each function in the vDSO ABI, define a symbol in the linker script
// pointing to its address.  The vDSO is loaded immediately after the
// userboot DSO image's last page, which is marked by the CODE_END symbol.
// So these symbols tell the linker where each vDSO function will be found
// at runtime.  The userboot code uses normal calls to these, declared as
// have hidden visibility so they won't generate PLT entries.  This results
// in the userboot binary having simple PC-relative calls to addresses
// outside its own image, to where the vDSO will be found at runtime.
#define FUNCTION(name, address, size) \
    PROVIDE_HIDDEN(name = CODE_END + address);
