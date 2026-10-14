#!/bin/sh
# Check that libass/libass.sym lists exactly the functions libass/ass.h
# declares, and optionally that a built shared library exports exactly them.
#
# usage: check-exports.sh [shared-library]
#   CC (default: cc) preprocesses ass.h, so conditional declarations count
#   as the compiler sees them.
set -eu

src=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

# Every "ass_name(" in the preprocessed header is a function declaration:
# ass.h has no inline functions, and function-pointer types are ASS_* names.
# Preprocess a .c file that includes it (tcc refuses to preprocess a .h).
echo '#include "ass.h"' > "$tmp/ass_h.c"
${CC:-cc} -E -I"$src/libass" "$tmp/ass_h.c" > "$tmp/ass_h.i"
tr '\n' ' ' < "$tmp/ass_h.i" \
    | grep -oE '(^|[^A-Za-z0-9_])ass_[A-Za-z0-9_]+ *\(' \
    | sed -E 's/^[^a]*//; s/ *\($//' \
    | sort -u > "$tmp/header"
sed -e 's/[[:space:]]//g' -e '/^$/d' "$src/libass/libass.sym" | sort -u > "$tmp/sym"

rc=0
report() {
    if [ -s "$2" ]; then
        echo "$1:"
        sed 's/^/    /' "$2"
        rc=1
    fi
}

comm -23 "$tmp/header" "$tmp/sym" > "$tmp/missing"
comm -13 "$tmp/header" "$tmp/sym" > "$tmp/extra"
report "Declared in ass.h but missing from libass.sym" "$tmp/missing"
report "Listed in libass.sym but not declared in ass.h" "$tmp/extra"

if [ $# -ge 1 ]; then
    nm -D --defined-only "$1" | awk '$2 == "T" { sub(/@.*/, "", $3); print $3 }' \
        | sort -u > "$tmp/lib"
    comm -23 "$tmp/sym" "$tmp/lib" > "$tmp/unexported"
    comm -13 "$tmp/sym" "$tmp/lib" > "$tmp/leaked"
    report "Listed in libass.sym but not exported by $1" "$tmp/unexported"
    report "Exported by $1 but not listed in libass.sym" "$tmp/leaked"
fi

[ $rc -ne 0 ] || echo "libass.sym matches ass.h ($(wc -l < "$tmp/sym") functions)"
exit $rc
