#!/usr/bin/env bash
# interleave.sh - run several configurations in RANDOM order, round by round.
#
# Why: if you run all of config A, then all of B, any slow drift in the
# machine (heat, background tasks, CPU speed) gets mixed up with A-vs-B.
# Shuffling the order every round spreads that drift evenly over all configs.
#
# Usage: ./interleave.sh <rounds> <frame_bytes> <fps> <num_frames> <config>...
#   each <config> is  label:consumer_cpu:producer_cpu   (leave cpus empty = unpinned)
# Example:
#   ./interleave.sh 15 5529600 50 1500 unp:: same:1:5 diff:1:2
set -e
ROUNDS=$1; FRAME=$2; FPS=$3; N=$4; shift 4
CONFIGS=("$@")
mkdir -p results
SUMMARY=results/summary.csv
[ -f $SUMMARY ] || echo "label,rep,frame_bytes,fps,frames,dropped,mean_us,p50_us,p99_us,p999_us,max_us,jitter_us" > $SUMMARY

pin() { if [ -n "$1" ]; then echo "taskset -c $1"; fi; }

for round in $(seq 1 $ROUNDS); do
  for cfg in $(printf '%s\n' "${CONFIGS[@]}" | shuf); do
    IFS=: read -r LABEL CCPU PCPU <<< "$cfg"
    NAME=/framebench_$$_${LABEL}_$round
    CSV=results/${LABEL}_r${round}.csv
    $(pin $CCPU) ./consumer $NAME $CSV 2>/dev/null &
    CPID=$!
    DROPPED=$($(pin $PCPU) ./producer $NAME $FRAME $FPS $N 2>/dev/null | sed 's/dropped=//')
    wait $CPID
    ROW=$(python3 analyze.py $CSV $DROPPED --row)
    echo "$LABEL,$round,$FRAME,$FPS,$ROW" >> $SUMMARY
    echo "round $round/$ROUNDS  $LABEL  p99 $(echo $ROW | cut -d, -f5) us"
    sleep 1
  done
done
