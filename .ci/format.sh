#!/usr/bin/env bash

# Fail when a C source or header differs from clang-format 20 output.
CLANG_FORMAT="${CLANG_FORMAT:-clang-format-20}"
SOURCES=$(git ls-files 'src/*.[ch]' 'common/*.[ch]' 'tests/*.[ch]')

ret=0
for file in ${SOURCES}; do
    if ! "$CLANG_FORMAT" "$file" | diff -u -p --label="${file}" \
        --label="expected coding style" "$file" -; then
        ret=1
    fi
done
exit $ret
