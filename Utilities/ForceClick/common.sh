#!/bin/bash
# Shared setup for measure.sh / validate.sh / firmness.sh. Sourced, not run.
# Checks the patched driver is loaded, saves its configuration and restores it on exit (also on Ctrl+C),
# and turns ForceClickDiagnostics on for the run.
set -u
FC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CTL="$FC_DIR/vi2c-forcectl"
PROBE="$FC_DIR/ForceProbe"
RESULTS="${RESULTS:-$FC_DIR/results}"
mkdir -p "$RESULTS"

[ -x "$CTL" ] && [ -x "$PROBE" ] || { echo "build the tools first: $FC_DIR/build.sh"; exit 1; }
"$CTL" get >/dev/null 2>&1 || { echo "VoodooI2CNativeEngine not found (is VoodooI2C loaded?)"; exit 1; }
[ "$("$CTL" get | awk '/^ForceClickDiagnostics =/ {print $3}')" = "-" ] && {
    echo "the running VoodooI2C has no Force Click support (ForceClickDiagnostics missing)"; exit 1; }
if [ "$(defaults read com.apple.AppleMultitouchTrackpad ForceSuppressed 2>/dev/null || echo 0)" = "1" ]; then
    echo "Turn on System Settings > Trackpad > \"Force Click and haptic feedback\" first."
    exit 1
fi

sudo -v || exit 1
FC_SAVED="$("$CTL" config)"
fc_restore() { [ -n "${FC_SAVED:-}" ] && sudo "$CTL" set $FC_SAVED >/dev/null && echo "driver configuration restored: $FC_SAVED"; }
trap fc_restore EXIT
trap 'exit 130' INT TERM

fc_set() { sudo "$CTL" set ForceClickDiagnostics=1 "$@" >/dev/null || { echo "setting $* failed"; exit 1; }; }
fc_show() { "$CTL" get | grep -E '^ForceClick(Mode|Threshold|Hysteresis|DwellMs|FollowSystem|FirmnessStep|EffectiveThreshold) '; }
fc_click_setting() { defaults read com.apple.AppleMultitouchTrackpad FirstClickThreshold 2>/dev/null || echo "?"; }

fc_instructions() {
    echo "  - press down and release every time; do not tap; one press at a time"
    echo "  - normal click: the force you use to click a link; hard press: the force you use for Force Click"
    echo "  - if a press felt wrong (double click, no reaction), do not repeat it, just go on"
}
