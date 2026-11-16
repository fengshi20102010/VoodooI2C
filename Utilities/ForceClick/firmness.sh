#!/bin/bash
# Step 3 - the macOS "Click" setting (light / medium / firm) must move the Force Click threshold.
#     THR=160 DWELL=0 STEP=20 ./firmness.sh    # N=10 normal clicks + 10 hard presses per setting
source "$(dirname "$0")/common.sh"
THR=${THR:-160}; DWELL=${DWELL:-0}; STEP=${STEP:-20}; N=${N:-10}
OUT="$RESULTS/firmness-thr$THR-s$STEP-$(date +%Y%m%d-%H%M%S).csv"
ORIG=$(fc_click_setting)
fc_set ForceClickMode=1 ForceClickThreshold="$THR" ForceClickHysteresis=40 ForceClickDwellMs="$DWELL" ForceClickFollowSystem=1 ForceClickFirmnessStep="$STEP"
for level in 0:Light 1:Medium 2:Firm; do
    want=${level%%:*}; name=${level##*:}
    echo; echo "=== System Settings > Trackpad > Click: choose \"$name\", then come back ==="
    read -r -p "Press Enter when done..." _
    [ "$(fc_click_setting)" != "$want" ] && { echo "Click setting reads $(fc_click_setting), expected $want"; read -r -p "Enter to continue anyway..." _; }
    echo "The driver re-reads the setting after 1 s without presses; waiting 2 s."; sleep 2
    "$PROBE" "FIRM-$want" "$N" "$OUT"
done
python3 - "$OUT" "$THR" "$STEP" <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1]))); thr, step = int(sys.argv[2]), int(sys.argv[3])
print("setting  expected  driver   false FC   missed   normal peak   hard peak")
for want, name in (("0", "light"), ("1", "medium"), ("2", "firm")):
    rs = [r for r in rows if r["condition"] == "FIRM-" + want]
    if not rs: continue
    li = [r for r in rs if r["phase"] == "light"]; ha = [r for r in rs if r["phase"] == "hard"]
    lp = sorted(int(r["driver_raw_max"]) for r in li); hp = sorted(int(r["driver_raw_max"]) for r in ha)
    print("%-8s %8d  %-7s  %3d/%-5d %3d/%-4d %4d-%-8d %d-%d" % (name, thr + (int(want) - 1) * step,
          ",".join(sorted({r["effective_threshold"] for r in rs})),
          sum(int(r["mac_max_stage"]) >= 2 for r in li), len(li), sum(int(r["mac_max_stage"]) < 2 for r in ha), len(ha),
          lp[0], lp[-1], hp[0], hp[-1]))
PY
echo "data: $OUT"
echo "Remember to set System Settings > Trackpad > Click back to your usual value (it was $ORIG)."
