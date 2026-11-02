#!/bin/bash
cd "$(dirname "$0")"
for t in 1 2 4 8; do
  echo "THREADS $t"
  for i in 1 2 3; do
    ./qsort -t "$t" -n 2000000 -s 1 2>/dev/null
  done
done
