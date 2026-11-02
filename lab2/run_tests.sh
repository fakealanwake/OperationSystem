#!/bin/bash
set -e
cd "$(dirname "$0")"
make
echo "=== small ==="
printf '8\n5 1 4 2 8 3 7 6\n' | ./qsort -t 2
echo "=== dups ==="
printf '6\n2 2 1 1 3 3\n' | ./qsort -t 2
echo "=== one ==="
printf '1\n42\n' | ./qsort -t 1
echo "=== reverse 5000 t=4 ==="
python3 -c 'print(5000); print(" ".join(str(5000-i) for i in range(5000)))' | ./qsort -t 4
echo "=== random timings ==="
for t in 1 2 4 8; do
  ./qsort -t "$t" -n 2000000 -s 1
done
echo "=== bad key ==="
set +e
./qsort
echo "exit:$?"
