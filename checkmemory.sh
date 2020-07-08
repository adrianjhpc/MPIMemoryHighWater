#!/bin/bash
hostname==`hostname`
rm -f memoutput${hostname}.txt
while :
do
  head -3 /proc/meminfo | grep 'MemFree' >> memoutput${hostname}.txt
  sleep 1
done
