#!/bin/bash
# Builds and runs the full evaluation used in the report.
#   1-core: 4 traces x {no prefetch, next_line+ip_stride} x {lru_crc2, ship_pp, custom}
#   4-core: 2 mixes (next_line+ip_stride) x {lru_crc2, ship_pp, custom}
# Usage: ./scripts/run_eval.sh   (traces must be in traces/)   then: python3 scripts/analyze.py
cd "$(dirname "$0")/.."
POLICIES="lru_crc2 ship_pp custom"; WARM=10000000; SIM=10000000; JOBS=${JOBS:-8}
T(){ echo traces/$1_30M.trace.gz; }
for c in 1 4; do for pf in "no no" "next_line ip_stride"; do for r in $POLICIES; do
  set -- $pf; b=bimodal-$1-$2-$r-${c}core
  [ "$c" = 4 ] && [ "$1" = no ] && continue
  [ -f bin/$b ] || bash build_champsim.sh bimodal $1 $2 $r $c > /dev/null || exit 1
done; done; done
mkdir -p results/1core results/4core
throttle(){ while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 2; done; }
run(){ throttle; ./bin/$1 -warmup_instructions $WARM -simulation_instructions $SIM -traces "${@:3}" > "$2" 2>&1 & }
for r in $POLICIES; do
  b=bimodal-next_line-ip_stride-$r-4core
  run $b results/4core/mix1-$r.txt $(T mcf) $(T mcf) $(T libquantum) $(T libquantum)
  run $b results/4core/mix2-$r.txt $(T mcf) $(T libquantum) $(T mcf) $(T xalancbmk)
  for pf in no-no next_line-ip_stride; do for t in bzip2 libquantum mcf xalancbmk; do
    run bimodal-$pf-$r-1core results/1core/$t-$pf-$r.txt $(T $t)
  done; done
done
wait
