#!/usr/bin/env python3
"""Replay the driver's mode-1 rule (threshold + hysteresis + dwell) on recorded press traces and pick
parameters.   python3 analyze.py results.csv [more.csv ...] [--hys 40] [--max-dwell 160] [--condition LABEL]

The rule is the same as VoodooI2CNativeEngine::updateForceStage: arm when pressure >= threshold, disarm
when pressure < threshold - hysteresis, become a Force Click once armed for >= dwell ms (checked on
each report). A light press that becomes a Force Click is a false positive (FP); a hard press that
does not is a miss. Choice: no FP, fewest misses, then the most robust point (largest number of
neighbouring grid points with the same result), then the lowest latency.
"""
import argparse, csv, statistics


def load(path, condition):
    out = []
    for r in csv.DictReader(open(path)):
        if condition and r["condition"] != condition:
            continue
        tr = [tuple(int(x) for x in e.split(":")) for e in r.get("trace", "").split(";") if e]
        if tr:
            out.append(("hard" if r["phase"] == "hard" else "light", tr))   # any non-hard phase must not force
    return out


def force_at(trace, thr, hys, dwell):
    exit_level, armed_at = thr - hys, None
    for ms, p in trace:
        if armed_at is not None and p < exit_level:
            armed_at = None
        if armed_at is None and p >= thr:
            armed_at = ms
        if armed_at is not None and ms - armed_at >= dwell:
            return ms
    return None


def evaluate(presses, thr, hys, dwell):
    fp = miss = 0
    lat = []
    for phase, tr in presses:
        t = force_at(tr, thr, hys, dwell)
        if phase == "light" and t is not None:
            fp += 1
        if phase == "hard":
            if t is None:
                miss += 1
            else:
                lat.append(t)
    return fp, miss, (statistics.median(lat) if lat else None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="+", help="one or more result files; presses are pooled")
    ap.add_argument("--hys", type=int, default=40)
    ap.add_argument("--condition")
    ap.add_argument("--max-dwell", type=int, default=160, help="longest dwell allowed in a recommendation (latency)")
    a = ap.parse_args()
    presses = [p for f in a.csv for p in load(f, a.condition)]
    nl = sum(p == "light" for p, _ in presses)
    nh = sum(p == "hard" for p, _ in presses)
    if not nl or not nh:
        raise SystemExit("need traces of both light and hard presses (record with the patched driver)")
    rate = [len(tr) / max(tr[-1][0], 1) * 1000 for _, tr in presses if len(tr) > 3]
    print("presses: light %d, hard %d; report rate ~%.0f Hz; hysteresis %d" % (nl, nh, statistics.median(rate), a.hys))
    for ph in ("light", "hard"):
        dur = [tr[-1][0] for p, tr in presses if p == ph]
        mx = [max(x for _, x in tr) for p, tr in presses if p == ph]
        print("  %-5s press length %d-%d ms (median %d), peak pressure %d-%d" % (ph, min(dur), max(dur),
              statistics.median(dur), min(mx), max(mx)))

    thrs = list(range(80, 241, 10))
    dwells = list(range(0, 401, 20))
    grid = {(t, d): evaluate(presses, t, a.hys, d) for t in thrs for d in dwells}

    print("\nmisclassified presses (FP light + missed hard) per threshold (rows) and dwell ms (columns):")
    print("thr\\dwell " + "".join("%4d" % d for d in dwells))
    for t in thrs:
        print("%-9d " % t + "".join("%4s" % ("%d" % (grid[(t, d)][0] + grid[(t, d)][1])) for d in dwells))

    feasible = [(k, v) for k, v in grid.items() if v[0] == 0 and k[1] <= a.max_dwell]
    if not feasible:
        print("\nno setting avoids all false Force Clicks on light presses")
        return
    best_miss = min(v[1] for _, v in feasible)
    cand = [(k, v) for k, v in feasible if v[1] == best_miss]

    def robustness(k):
        t, d = k
        n = 0
        for dt in (-10, 0, 10):
            for dd in (-20, -10, 0, 10, 20):
                v = grid.get((t + dt, d + dd))
                n += v is not None and v[0] == 0 and v[1] == best_miss
        return n
    cand.sort(key=lambda kv: (-robustness(kv[0]), kv[1][2] if kv[1][2] is not None else 1e9, kv[0][1]))
    print("\nbest settings without false Force Clicks (missed hard presses %d/%d):" % (best_miss, nh))
    print("  threshold dwell_ms  robustness(of 15)  median_force_latency_ms")
    for (t, d), (fp, miss, lat) in cand[:8]:
        print("  %9d %8d %18d %24s" % (t, d, robustness((t, d)), "-" if lat is None else "%d" % lat))
    (t, d), (fp, miss, lat) = cand[0]
    print("\nRECOMMENDED: ForceClickThreshold=%d ForceClickDwellMs=%d (hysteresis %d): FP 0/%d, missed %d/%d, "
          "median latency %s ms" % (t, d, a.hys, nl, miss, nh, "-" if lat is None else "%d" % lat))
    print("validate on NEW presses:  THR=%d DWELL=%d ./validate.sh" % (t, d))
    if len(a.csv) == 1:
        print("WARNING: one session only. How hard you press drifts between sessions; pool several sessions\n"
              "         (python3 analyze.py results/measure-*.csv) before you change the defaults.")

    # macOS Click setting: light = threshold - step, firm = threshold + step (ForceClickFirmnessStep).
    # Light presses measured here are "normal clicks"; at the light setting they must still not become
    # Force Clicks too often, and at the firm setting hard presses must still reach Force Click.
    print("\nForceClickFirmnessStep candidates (with threshold %d, dwell %d):" % (t, d))
    print("  step  light-setting thr  normal clicks forced   firm-setting thr  hard presses missed")
    for step in (10, 15, 20, 25, 30, 40):
        lo, hi = max(t - step, a.hys + 1), min(t + step, 255)
        fp_lo = evaluate(presses, lo, a.hys, d)[0]
        miss_hi = evaluate(presses, hi, a.hys, d)[1]
        print("  %4d  %17d  %13d/%-6d %17d  %12d/%d" % (step, lo, fp_lo, nl, hi, miss_hi, nh))
    print("  (pressure saturates at 255, so the firm side has little room above ~215)")


if __name__ == "__main__":
    main()
