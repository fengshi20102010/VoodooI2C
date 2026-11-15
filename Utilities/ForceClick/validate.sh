#!/bin/bash
# Step 2 - validation on NEW presses with fixed parameters (the Click setting is not followed here).
#     THR=180 DWELL=0 ./validate.sh            # N=20 normal clicks + 20 hard presses
# Pass: every press is exactly one click; no normal click becomes a Force Click; hard presses reach it.
source "$(dirname "$0")/common.sh"
THR=${THR:-180}; HYS=${HYS:-40}; DWELL=${DWELL:-0}; N=${N:-20}
LABEL="VALIDATE-thr$THR-d$DWELL"
OUT="$RESULTS/validate-thr$THR-d$DWELL-$(date +%Y%m%d-%H%M%S).csv"
fc_set ForceClickMode=1 ForceClickThreshold="$THR" ForceClickHysteresis="$HYS" ForceClickDwellMs="$DWELL" ForceClickFollowSystem=0
fc_show
echo "Validation: $N normal clicks + $N hard presses."
fc_instructions
read -r -p "Press Enter to start..." _
C0=$("$CTL" get | awk '/^ForceClickPressCount =/ {print $3}')
"$PROBE" "$LABEL" "$N" "$OUT"
C1=$("$CTL" get | awk '/^ForceClickPressCount =/ {print $3}')
python3 - "$OUT" "$C0" "$C1" <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
for ph, want_force in (("light", False), ("hard", True)):
    rs = [r for r in rows if r["phase"] == ph]
    one = sum(int(r["mac_clicks"]) == 1 for r in rs)
    fc = sum(int(r["mac_max_stage"]) >= 2 for r in rs)
    raw = sorted(int(r["driver_raw_max"]) for r in rs)
    bad = (fc if not want_force else len(rs) - fc)
    print("%-5s exactly one click %d/%d | Force Click %d/%d | %s %d/%d | peak pressure %d-%d (median %d)" % (
        ph, one, len(rs), fc, len(rs), "missed" if want_force else "false Force Click", bad, len(rs),
        raw[0], raw[-1], raw[len(raw) // 2]))
    for r in rs:
        if int(r["mac_clicks"]) != 1:
            print("      press %s: %s clicks, peak %s" % (r["trial"], r["mac_clicks"], r["driver_raw_max"]))
try:
    print("driver press count %d, recorded %d%s" % (int(sys.argv[3]) - int(sys.argv[2]), len(rows),
          "" if int(sys.argv[3]) - int(sys.argv[2]) == len(rows) else "  (mismatch: button bounce or presses outside the window)"))
except ValueError:
    pass
PY
echo "data: $OUT"
