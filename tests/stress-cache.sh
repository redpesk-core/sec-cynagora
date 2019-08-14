#!/bin/bash

me=$(basename $0 .sh)
d=$(mktemp -d /tmp/${me}.dirXXX)

# initial database
i=$d/ini
for((x = 0 ; x < 1000 ; x++))
do
	echo "CLIENT$x * USER$x PERMISSION$x yes forever"
done > $i

# run daemon
cynarad -i $i -d $d -S $d -l &
pc=$!
sleep 2

# stress the cache
echo ----------- START ------------------
for((y = 0 ; y < 10 ; y++)); do
  for((x = 0 ; x < 100 ; x++)); do
    echo "check CLIENT$x SESSION$x USER$x PERMISSION$x"
    sleep .05
  done
done |
valgrind cynadm -c 2000 -e -s $d/cynara.check
echo ----------- STOP ------------------

# terminate
kill $pc
rm -rf $d

