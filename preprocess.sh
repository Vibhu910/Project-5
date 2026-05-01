#/bin/bash

#cat $1 | grep "SCHED_SWITCH" | cut -c 39- | grep -o "^[0-9]*\s+[a-zA-Z0-9_]*\|vlag=[0-9]*"
cat $1 | grep "SCHED_SWITCH" | cut -c 39- | grep -o "^.....................\|vlag=[0-9]*\|slice=[0-9]*"
