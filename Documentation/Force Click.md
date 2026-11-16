#<cldoc:Force Click>
&#8291;
## Force Click on pressure pads

A *pressure pad* (Windows Precision Touchpad button type 1, "non-depressible") has no mechanical button. The
firmware reports a click itself once the finger pressure crosses its own, fairly low, threshold, and it reports
the finger pressure as `TipPressure` (0-255). Example: the Synaptics TM3572-001 (`06CB:CDF4`) in the Lenovo
XiaoXin Pro 13s.

VoodooI2C's Force Touch emulation (Native engine) turns every physical press into a Force Click. On a pressure
pad that means every normal click opens Look Up. `ForceClickMode 1` uses the pressure instead:

- a press is a **normal click** until `TipPressure` reaches the threshold;
- it becomes a **Force Click** once the pressure has reached the threshold and then stayed at or above the
  *exit level* (threshold - hysteresis) for `ForceClickDwellMs`;
- it stays a Force Click until the pressure drops below the exit level or the press ends.

The threshold follows the macOS **Click** setting (System Settings > Trackpad > Click): light lowers it by
`ForceClickFirmnessStep`, firm raises it by the same amount.

Touchpads that never report a non-zero pressure, and two-button touchpads, keep the original behaviour.
Force Click must be enabled in System Settings ("Force Click and haptic feedback") as before. This does not add
haptic feedback: the pad's own click feedback (if any) is unchanged.

## Limitations

**No variable pressure.** The driver reads the real `TipPressure` but uses it only to decide between a normal
click and a Force Click. VoodooInput emulates a Magic Trackpad 2 and fills the pressure macOS sees as follows:

| What the engine reports | Pressure macOS receives |
| --- | --- |
| finger resting, `supportsPressure` false | 5 |
| physical button down (normal click) | 120, whatever the real pressure |
| button cleared, `supportsPressure` true (Force Click) | the value set by the engine: 255 |

So macOS gets three levels. Features that vary with force inside a Force Click (the faster fast-forward in
QuickTime, line width in Markup, zoom speed in Maps) only see "Force Click or not"; Look Up, previews, Quick
Look, rename and App Exposé work normally.

Passing the real pressure with the button cleared makes macOS decide the click stage itself. That was measured
(run 1 below) and dropped: clicks and Force Clicks were lost. A possible extension, not implemented: keep the
button for the normal click and, only during the Force Click, map `TipPressure` from the threshold to 255 onto
the range above macOS's second stage (about 210-255 of the reported pressure). Open points: macOS's exit level
for the second stage is unknown (the stage could flicker), the range is narrow, and few applications use it.

**No haptic feedback.** On the TM3572-001 the actuator is not exposed through the HID Haptics page (0x0E) or
any RMI4 function (F01, F12, F34, F54, F55); its vendor reports 0xA0-0xA3 are undocumented. A Force Click
gives no click feel, only the on-screen result.

## Configuration

| Key | Default | Range | Meaning |
| --- | --- | --- | --- |
| `ForceClickMode` | 1 | 0-1 | 0 = original emulation (every press is a Force Click), 1 = pressure threshold |
| `ForceClickThreshold` | 160 | 1-255 | `TipPressure` that starts a Force Click (at the "medium" Click setting) |
| `ForceClickHysteresis` | 40 | 0-254 | exit level = threshold - hysteresis; must be below the threshold |
| `ForceClickDwellMs` | 0 | 0-1000 | time the pressure must stay at or above the exit level after reaching the threshold |
| `ForceClickFollowSystem` | 1 | 0-1 | move the threshold with the macOS Click setting |
| `ForceClickFirmnessStep` | 20 | 0-100 | threshold change per Click step (light -1, medium 0, firm +1) |
| `ForceClickDiagnostics` | 0 | 0-1 | publish per-press data (below) for tuning |

The values are read in this order, later wins:

1. the defaults above, which are also the keys of the `Native Multitouch Engine` personality in
   `VoodooI2C.kext/Contents/Info.plist`;
2. boot arguments: `vi2c-force-mode=0|1` and `vi2c-force-threshold=<hysteresis+1..255>`
   (`vi2c-force-threshold=0` is the same as `vi2c-force-mode=0`); invalid values are ignored and logged;
3. at runtime, for an administrator: `IORegistryEntrySetCFProperties` on `VoodooI2CNativeEngine` with any of
   the keys, for example `sudo Utilities/ForceClick/vi2c-forcectl set ForceClickThreshold=170`. Values out of
   range or a hysteresis that is not below the threshold are rejected and nothing changes. Runtime changes are
   lost at reboot.

Status published on `VoodooI2CNativeEngine`: the configuration keys, `ForceClickEffectiveThreshold` (threshold
in use after the Click setting), `ForceClickSystemFirmness` (0 light, 1 medium, 2 firm) and
`ForceClickPressureSeen`.

The Click setting is read together with the Force Click switch, which VoodooI2C re-reads only after one second
without presses; after changing it, wait a moment before the next press.

### Diagnostics

With `ForceClickDiagnostics=1` each finished press publishes `ForceClickPressCount`, `ForceClickLastPressMax`
(highest `TipPressure`), `ForceClickLastPressForced` and `ForceClickLastPressTrace` (per report: milliseconds
since the press started as 2 bytes little endian, then the pressure as 1 byte; up to 200 reports). Leave it off
in daily use.

## Tuning a touchpad

`Utilities/ForceClick` contains the tools used to choose the defaults:

| File | Purpose |
| --- | --- |
| `build.sh` | builds `vi2c-forcectl` and `ForceProbe` (Xcode command line tools) |
| `vi2c-forcectl` | `get`, `config`, `set KEY=VALUE...`, `watch` (live peak pressure of each press) |
| `ForceProbe` | test window: per physical press it records what macOS delivered (mouse-down count, highest `NSEvent.stage` and pressure) and the driver's trace |
| `measure.sh` | step 1: N normal clicks + N hard presses, then `analyze.py` |
| `analyze.py` | replays the mode 1 rule on recorded traces for every threshold / dwell pair and lists false Force Clicks, missed Force Clicks and latency; pools several files |
| `validate.sh` | step 2: fixed `THR` / `DWELL` on new presses, prints the results per phase |
| `firmness.sh` | step 3: light / medium / firm Click setting, checks that the threshold moves |
| `samples/tm3572-run*.csv` | the five sessions on a Synaptics TM3572-001 used below (runs 3-5 include traces) |

The scripts save the driver configuration, enable diagnostics for the run and restore the configuration when
they end. Every press must be pressed down (a tap is not a physical press and would count as an extra click).

Procedure:

1. `./build.sh`, then run `./measure.sh` in **several sessions**: how hard one presses changes between
   sessions, and parameters picked from one session do not carry over (see below).
2. `python3 analyze.py results/measure-*.csv` and pick the setting without false Force Clicks and with the
   fewest misses. Prefer a setting whose neighbours give the same result (robustness column) and keep the dwell
   short (default limit 160 ms).
3. Check it on **new** presses: `THR=... DWELL=... ./validate.sh`, then `THR=... DWELL=... ./firmness.sh`.
   Pass: every press is exactly one click, no normal click becomes a Force Click, and few hard presses miss.

## Reference measurements: Synaptics TM3572-001 (Lenovo XiaoXin Pro 13s, macOS 15)

Click setting "medium". "False FC" = normal click delivered as a Force Click, "missed" = hard press that did not
reach Force Click.

Run 1 compared the approaches (10 normal clicks + 10 hard presses each):

| Approach | Normal: one click | Normal: false FC | Hard: one click | Hard: Force Click |
| --- | --- | --- | --- | --- |
| original emulation | 7/10 | 10/10 | 10/10 | 10/10 |
| threshold 140 (mode 1) | 9/10 | 0/10 | 10/10 | 10/10 |
| pressure mapped to 0-255, macOS decides (linear) | 10/10 | 0/10 | 10/10 | 8/10 |
| same, squared curve | 5/10 (5 no click) | 0/10 | 10/10 | 7/10 |

Passing the pressure to macOS and letting it pick the stage needs no device button, but it lost clicks and
Force Clicks; that variant was dropped. From the mapped runs macOS appeared to start a click at about 50/255 and
a Force Click at about 210-220/255 of the reported pressure (few samples).

Peak `TipPressure` per press (run 1: original emulation and threshold 140 together; run 2: threshold 170;
run 3: measurement with traces; run 4: validation at 180; run 5: Click setting light / medium / firm at
160 / 180 / 200, 10 + 10 presses each):

| Run | Normal clicks | Hard presses (median) |
| --- | --- | --- |
| 1 | 55-132 | 189-255 (255) |
| 2 | 66-172 | 146-255 (255) |
| 3 | 59-115 | 82-255 (216) |
| 4 | 51-150 | 109-255 (200) |
| 5 light / medium / firm | 63-116 / 53-126 / 60-139 | 86-250 (124) / 116-179 (121) / 191-255 (255) |

How hard a "hard press" is drifts a lot between sessions (median 121 to 255), while normal clicks stay
below about 150 with rare peaks up to 172. Pooled over all sessions (110 normal clicks, 90 hard presses at the
medium setting):

| Threshold | False FC | Missed |
| --- | --- | --- |
| 140 | 7 (6.4%) | 11 (12.2%) |
| 150 | 6 (5.5%) | 12 (13.3%) |
| **160** | **3 (2.7%)** | **18 (20.0%)** |
| 170 | 2 (1.8%) | 21 (23.3%) |
| 180 | 0 | 26 (28.9%) |

A false Force Click (Look Up instead of opening a link) is worse than a miss (press a little harder), so the
medium default stays on the low-false side, but 180 (chosen after runs 1-3) missed 18 of the 30 hard presses
at the medium setting in runs 4-5. The default is 160; with the step of 20 the Click setting gives light = 140 (Force Click easily),
medium = 160, firm = 180 (no false Force Click in any session).

Extra clicks: with mode 1, 1 of 200 presses produced two clicks (run 1, a normal click with peak 109, no Force
Click involved); with the original emulation 3 of 10 normal clicks did. In runs 4-5 (100 presses) the
driver's decision matched the stage macOS delivered for every press, every press was exactly one click, and
the effective threshold followed the Click setting (160 / 180 / 200).

Dwell time (run 3, with traces, `samples/measure-tm3572-001.csv`): for every threshold the result was the same
for dwell times from 0 to 160 ms, so in these presses the normal clicks never crossed a threshold briefly.
The only setting without errors was threshold 80 with 300 ms dwell; it separates the presses by how long they
are held, not by force, adds about 330 ms of latency and has no neighbours with the same result, so it was not
used. `analyze.py` on this run alone suggests threshold 120, which run 2 (normal clicks up to 172) contradicts:
use several sessions. On the traced runs 3-5 (70 normal clicks, 50 hard presses at medium) a dwell removes
at most one false Force Click at threshold 140-150 and costs 120-160 ms; at 160 it changes nothing.
`ForceClickDwellMs` stays available with a default of 0.

Click setting step: 20 keeps light (140) at 6.4% false Force Clicks; 30 would put light at 130 (10.9%).

Not yet measured: other pressure pads. Values for other devices may differ; use the procedure above and report
them.
