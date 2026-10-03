#!/usr/bin/env sh
# Builds everything. Needs: gcc (or clang) and clang with the wasm32 target + wasm-ld.
set -e
cd "$(dirname "$0")"
mkdir -p build public/data public/src

# 1. pack IMD's yearly files into public/data/season.bin (native C tool)
gcc -std=c99 -O2 -Wall -Wextra tools/prep.c -o build/prep
./build/prep data public/data/season.bin 2015 2016 2017 2018 2019 2020 2021 2022 2023 2024 2025

# 2. native tests of the same C core
gcc -std=c99 -O1 -Wall -Wextra -Wno-misleading-indentation -fno-builtin core/*.c tests/test_core.c -o build/test_core
./build/test_core > build/test_out.txt && echo "native tests passed"

# 3. the C core -> WebAssembly, with no standard library at all.
#    Only the hh_* functions in app.c are exported to the page.
EXPORTS=$(grep -ho "hh_[a-z_]*(" core/app.c | sort -u | tr -d '(' | sed 's/^/-Wl,--export=/' | tr '\n' ' ')
clang --target=wasm32 -nostdlib -O2 -Wall -Wextra -Wno-misleading-indentation -fno-builtin \
  -Wl,--no-entry $EXPORTS -Wl,--initial-memory=8388608 -Wl,-z,stack-size=1048576 \
  core/*.c -o public/core.wasm
echo "built public/core.wasm ($(wc -c < public/core.wasm) bytes)"

# 4. copy the C files so Lab mode can show the real source
cp core/*.c core/*.h public/src/
