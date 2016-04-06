## versioner
Use clang to programmatically add availability attributes to Android's libc headers.

#### Build
Copy to a recent AOSP tree, and build with `FORCE_BUILD_LLVM_COMPONENTS=true mma -j48`

#### Tools
##### unique_decl
Identify declarations that appear in multiple source files, so that they can be extracted to `<bits/foo.h>` to allow for availability tagging in a single location.
