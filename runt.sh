#!/bin/sh
# usage: runt.sh TAG EFFORT names...   (targets in /tmp/targets/NAME.raw; env passes through)
TAG=$1; EF=$2; shift 2
for n in "$@"; do
  r=$(timeout 200 ./run_new_t /tmp/targets/$n.raw /tmp/res/${TAG}_$n.txt $EF 2>/dev/null | sed 's/.*note=\([0-9]*\) freq=\([0-9.]*\).*loss=\([0-9.]*\).*time=\(.*\)/root \1 loss \3 \4/')
  echo "$n: $r | $(./eval /tmp/targets/$n.raw /tmp/res/${TAG}_$n.txt | cut -c16-75)" | tee -a /tmp/res/$TAG.t
done
