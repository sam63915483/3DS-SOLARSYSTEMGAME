#!/bin/bash
# Builds every tests/test_*.c against source/core with the host gcc and runs it.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/tests
status=0
for t in tests/test_*.c; do
  name=$(basename "$t" .c)
  gcc -std=c99 -Wall -Wextra -Werror -O1 -Isource/core -Itests "$t" source/core/*.c -lm -o "build/tests/$name.exe"
  echo "== $name"
  "build/tests/$name.exe" || status=1
done
exit $status
