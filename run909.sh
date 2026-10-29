#!/bin/sh
# usage: run909.sh TAG EFFORT names...   (final loss + independent metrics)
TAG=$1; EF=$2; shift 2
for n in "$@"; do
  r=$(timeout 110 ./run_new_t /tmp/targets/909_$n.raw /tmp/res/${TAG}_909_$n.txt $EF 2>/dev/null | sed 's/.*note=\([0-9]*\) freq=\([0-9.]*\).*loss=\([0-9.]*\).*time=\(.*\)/root \1 f0 \2 loss \3 \4/')
  echo "$n: $r | $(./eval /tmp/targets/909_$n.raw /tmp/res/${TAG}_909_$n.txt | cut -c16-100)" | tee -a /tmp/res/$TAG.909
done
