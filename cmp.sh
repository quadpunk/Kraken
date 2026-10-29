#!/bin/sh
# usage: cmp.sh LABEL "ENV" EFFORT targets...   prints final loss per target (the optimisation objective)
L=$1; E=$2; EF=$3; shift 3
for t in "$@"; do
  r=$(env $E timeout 110 ./run_new_t /tmp/targets/$t.raw /tmp/res/cmp_${L}_$t.txt $EF 2>/dev/null | sed 's/.*loss=\([0-9.]*\).*time=\(.*\)/\1 \2/')
  echo "$L $t $r" | tee -a /tmp/res/cmp.log
done
