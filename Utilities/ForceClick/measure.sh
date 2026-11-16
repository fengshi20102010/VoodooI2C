#!/bin/bash
# Step 1 - measurement: records the pressure trace of N normal clicks + N hard presses, then replays the
# mode-1 rule on them (analyze.py) and suggests ForceClickThreshold / ForceClickDwellMs / FirmnessStep.
#     ./measure.sh                 # N=20
# While you press, the driver runs threshold 160 without dwell and without following the Click setting;
# that only affects what you feel, not the recorded traces.
source "$(dirname "$0")/common.sh"
N=${N:-20}
OUT="$RESULTS/measure-$(date +%Y%m%d-%H%M%S).csv"
fc_set ForceClickMode=1 ForceClickThreshold=160 ForceClickHysteresis=40 ForceClickDwellMs=0 ForceClickFollowSystem=0
echo "Measurement: $N normal clicks + $N hard presses in the ForceProbe window."
fc_instructions
read -r -p "Press Enter to start..." _
"$PROBE" MEASURE "$N" "$OUT"
echo
python3 "$FC_DIR/analyze.py" "$OUT" --hys 40
echo "data: $OUT"
echo "Pool several sessions before choosing values:  python3 analyze.py $RESULTS/measure-*.csv"
