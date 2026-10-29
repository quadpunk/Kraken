#!/bin/sh
# usage: bench.sh TAG EFFORT target1 target2 ...   (appends to /tmp/res/TAG.summary)
TAG=$1; EFF=$2; shift 2
for t in "$@"; do
  timeout 200 ./run_new /tmp/targets/$t.raw /tmp/res/${TAG}_$t.txt $EFF | sed 's/.*: ok/ok/' | cut -c1-110 | tr '\n' ' ' >> /tmp/res/$TAG.log
  ./eval /tmp/targets/$t.raw /tmp/res/${TAG}_$t.txt | tee -a /tmp/res/$TAG.summary
done
