#!/usr/bin/env bash
# sweep.sh - run ONE configuration several times and append a summary row
# per run to results/summary.csv. One row per run lets you report the spread
# and run statistics (e.g. ANOVA) later.
#
# Usage: ./sweep.sh <label> <reps> <frame_bytes> <fps> <num_frames> [consumer_cpu] [producer_cpu]
# Examples:
#   ./sweep.sh 1080p_native 10 5529600 50 500
#   ./sweep.sh 1080p_pinned 10 5529600 50 500 1 0
#   ./sweep.sh 4k_native    10 22118400 50 300
set -e
LABEL=$1; REPS=$2; FRAME=$3; FPS=$4; N=$5; CCPU=$6; PCPU=$7
mkdir -p results
SUMMARY=results/summary.csv
if [ ! -f $SUMMARY ]; then
  echo "label,rep,frame_bytes,fps,frames,dropped,mean_us,p50_us,p99_us,p999_us,max_us,jitter_us" > $SUMMARY
fi

pin() { if [ -n "$1" ]; then echo "taskset -c $1"; fi; }

for rep in $(seq 1 $REPS); do
  NAME=/framebench_$$_$rep
  CSV=results/${LABEL}_rep${rep}.csv
  $(pin $CCPU) ./consumer $NAME $CSV 2>/dev/null &
  CPID=$!
  DROPPED=$($(pin $PCPU) ./producer $NAME $FRAME $FPS $N 2>/dev/null | sed 's/dropped=//')
  wait $CPID
  ROW=$(python3 analyze.py $CSV $DROPPED --row)
  echo "$LABEL,$rep,$FRAME,$FPS,$ROW" >> $SUMMARY
  echo "[$LABEL] rep $rep/$REPS done: $(echo $ROW | cut -d, -f5) us p99"
  sleep 1   # let the system settle between runs
done
