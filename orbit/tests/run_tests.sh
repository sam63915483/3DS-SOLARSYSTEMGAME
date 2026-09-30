#!/bin/bash
# Builds every tests/test_*.c against the portable sim files with the host gcc and runs it.
set -e
export PATH="/ucrt64/bin:$PATH"
cd "$(dirname "$0")/.."
mkdir -p build/tests
status=0
for t in tests/test_*.c; do
  name=$(basename "$t" .c)
  gcc -std=gnu99 -Wall -Wextra -Werror -O1 -Isource -Itests "$t" source/world.c source/player.c source/camera.c source/mesh.c source/noise.c source/fight.c source/fishing.c source/school.c -lm -o "build/tests/$name.exe"
  echo "== $name"
  "build/tests/$name.exe" || status=1
done
exit $status
