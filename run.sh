#!/usr/bin/env bash
# run.sh - one benchmark run. Prints a latency summary.
# Usage: ./run.sh <frame_bytes> <fps> <num_frames> [label] [consumer_cpu] [producer_cpu]
# Example: ./run.sh 5529600 50 500 1080p_native 1 0
set -e
FRAME=$1; FPS=$2; N=$3; LABEL=${4:-run}; CCPU=$5; PCPU=$6
NAME=/framebench_$$
mkdir -p results
CSV=results/${LABEL}.csv

pin() { if [ -n "$1" ]; then echo "taskset -c $1"; fi; }

$(pin $CCPU) ./consumer $NAME $CSV &
CPID=$!
DROPPED=$($(pin $PCPU) ./producer $NAME $FRAME $FPS $N | sed 's/dropped=//')
wait $CPID
python3 analyze.py $CSV $DROPPED
