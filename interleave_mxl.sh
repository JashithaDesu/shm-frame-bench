#!/usr/bin/env bash
# interleave_mxl.sh - compare the raw shared-memory pipeline with MXL,
# in randomized order, round by round (same idea as interleave.sh).
#
# Usage: ./interleave_mxl.sh <rounds> <1080|2160> <num_frames> <config>...
#   each <config> is  kind:label:consumer_cpu:producer_cpu
#   kind = shm        producer.c / consumer.c (busy-wait consumer)
#          mxl-block  MXL writer / reader, reader sleeps until each commit
#          mxl-spin   MXL writer / reader, reader busy-polls (like consumer.c)
# Example:
#   ./interleave_mxl.sh 15 1080 1500 shm:shm:1:2 mxl-block:mxlb:1:2 mxl-spin:mxls:1:2
# All runs are 50 fps. Results go to results/summary.csv like the other scripts.
set -e
ROUNDS=$1; RES=$2; N=$3; shift 3
CONFIGS=("$@")
case $RES in
  1080) FRAME=5529600;  FLOW=mxl/flow-1080p50.json; FLOW_ID=b7e3c1a0-5d2f-4e8a-9c41-0a1b2c3d4e51 ;;
  2160) FRAME=22118400; FLOW=mxl/flow-2160p50.json; FLOW_ID=b7e3c1a0-5d2f-4e8a-9c41-0a1b2c3d4e52 ;;
  *) echo "resolution must be 1080 or 2160"; exit 1 ;;
esac
FPS=50
DOMAIN=/dev/shm/mxl-bench
mkdir -p results $DOMAIN
SUMMARY=results/summary.csv
[ -f $SUMMARY ] || echo "label,rep,frame_bytes,fps,frames,dropped,mean_us,p50_us,p99_us,p999_us,max_us,jitter_us" > $SUMMARY

pin() { if [ -n "$1" ]; then echo "taskset -c $1"; fi; }

for round in $(seq 1 $ROUNDS); do
  for cfg in $(printf '%s\n' "${CONFIGS[@]}" | shuf); do
    IFS=: read -r KIND LABEL CCPU PCPU <<< "$cfg"
    CSV=results/${LABEL}_r${round}.csv
    case $KIND in
      shm)
        NAME=/framebench_$$_${LABEL}_$round
        $(pin $CCPU) ./consumer $NAME $CSV 2>/dev/null &
        CPID=$!
        DROPPED=$($(pin $PCPU) ./producer $NAME $FRAME $FPS $N 2>/dev/null | sed 's/dropped=//')
        wait $CPID ;;
      mxl-block|mxl-spin)
        MODE=${KIND#mxl-}
        $(pin $PCPU) ./mxl/mxl_writer $DOMAIN $FLOW $N 2>/dev/null &
        WPID=$!
        DROPPED=$($(pin $CCPU) ./mxl/mxl_reader $DOMAIN $FLOW_ID $N $CSV $MODE 2>/dev/null | sed 's/dropped=//')
        wait $WPID ;;
      *) echo "unknown kind $KIND"; exit 1 ;;
    esac
    ROW=$(python3 analyze.py $CSV $DROPPED --row)
    echo "$LABEL,$round,$FRAME,$FPS,$ROW" >> $SUMMARY
    echo "round $round/$ROUNDS  $LABEL  p99 $(echo $ROW | cut -d, -f5) us  dropped $DROPPED"
    sleep 1
  done
done
