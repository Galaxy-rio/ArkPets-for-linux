#!/usr/bin/env python3
"""Opt-in real Hyprland activity/drag test, using a separately downloaded Shu."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

import gi
gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--model", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--monitor", required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)


def hypr(command):
    return json.loads(subprocess.check_output(["hyprctl", "-j", command], text=True))


def evaluate(code):
    result = subprocess.check_output(["hyprctl", "eval", code], text=True).strip()
    assert result == "ok", result


def pet():
    found = [entry for monitor in hypr("layers").values()
             for entries in monitor["levels"].values() for entry in entries
             if entry["namespace"] == "arkpets-hyprland"]
    assert len(found) <= 1
    return found[0] if found else None


def move(x, y):
    evaluate(f"hl.dispatch(hl.dsp.cursor.move({{x={int(x)},y={int(y)}}}))")


def hit_point(image):
    """Find an opaque interior shared by the rendered pet and its mirror."""
    pix = GdkPixbuf.Pixbuf.new_from_file(str(image))
    w, h, stride = pix.get_width(), pix.get_height(), pix.get_rowstride()
    pixels = pix.get_pixels()
    assert pix.get_n_channels() == 4
    candidates = []
    for y in range(16, h-16, 4):
        for x in range(16, w-16, 4):
            if all(pixels[yy*stride+xx*4+3] > 230
                   and pixels[yy*stride+(w-1-xx)*4+3] > 230
                   for yy in range(y-12, y+13, 4) for xx in range(x-12, x+13, 4)):
                candidates.append((abs(x-w/2)+abs(y-h*.53), x, y))
    assert candidates, "No safe opaque target for the test pointer"
    _, x, y = min(candidates)
    return x/w, y/h


assert pet() is None, "Stop the existing pet before running this test"
monitor = next(m for m in hypr("monitors") if m["name"] == args.monitor)
width = round((monitor["height"] if monitor["transform"] % 2 else monitor["width"]) / monitor["scale"])
original_cursor = hypr("cursorpos")
focus = hypr("activewindow").get("address")
base = [str(ROOT/"bin/arkpets-renderer"), "--model", str(args.model.resolve()),
        "--scale", "0.86", "--monitor", args.monitor, "--auto-activity", "--activity-seed", "0"]
report = {"monitor": args.monitor, "seed": 0}
evaluate('if _G.arkpets_activity_test then _G.arkpets_activity_test:set_enabled(true) '
         'else _G.arkpets_activity_test=hl.layer_rule({name="arkpets-activity-test",'
         'match={namespace="^arkpets-hyprland$"},blur=false,blur_popups=false,dim_around=false,no_anim=true}) end')
try:
    for dragging, duration in ((False, 31), (True, 18)):
        folder = args.output / ("drag" if dragging else "states")
        folder.mkdir(exist_ok=True)
        logfile = folder/"renderer.log"
        positions, did_drag = [], False
        right = 800 if dragging else width
        command = base + ["--right", str(right), "--duration", str(duration), "--capture-dir", str(folder.resolve())]
        with logfile.open("w") as log:
            child = subprocess.Popen(command, stdout=log, stderr=log,
                                     env={**os.environ, "GSETTINGS_BACKEND": "memory"})
            try:
                deadline = time.monotonic()+duration+6
                while child.poll() is None and time.monotonic() < deadline:
                    surface = pet()
                    if surface:
                        positions.append(surface["x"])
                        assert monitor["x"] <= surface["x"] <= monitor["x"]+width-surface["w"]
                        assert hypr("activewindow").get("address") == focus
                    content = logfile.read_text()
                    if dragging and not did_drag and "\twalking" in content and surface:
                        dx, dy = hit_point(folder/"frame-1.png")
                        move(surface["x"]+dx*surface["w"], surface["y"]+dy*surface["h"])
                        subprocess.run([str(ROOT/"bin/drag-probe"), "-90", "-60"], check=True, timeout=5)
                        content = logfile.read_text()
                        assert "\tpaused" in content, content
                        after = pet()
                        assert after["y"] <= surface["y"]-50
                        time.sleep(.8)
                        assert pet()["x"] == after["x"], "Activity did not pause after dragging"
                        report["drag"] = {"before": surface, "after": after, "paused_and_resumed": True}
                        did_drag = True
                    time.sleep(.08)
                assert child.wait(timeout=3) == 0
            finally:
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=5)
        content = logfile.read_text()
        events = [line.split(" ", 1)[1].split("\t") for line in content.splitlines() if line.startswith("ARKPETS_ACTIVITY ")]
        assert pet() is None
        assert "错误:" not in content
        if dragging:
            assert did_drag
            pause = next(i for i, event in enumerate(events) if event[2] == "paused")
            assert len(events[pause+1:]) >= 2, "Scheduler did not resume after dragging"
            report["drag"]["events"] = events
        else:
            assert {"Relax", "Move", "Sit", "Sleep"} <= {e[0] for e in events}, events
            assert min(positions) <= monitor["x"]+2, "Pet did not reach the edge"
            assert positions[-1] > positions[0]+100, "Pet did not reverse at the edge"
            report["activities"] = events
            report["edge_turn"] = {"start_x": positions[0], "minimum_x": min(positions), "last_x": positions[-1]}
    report["result"] = "PASS"
    report["keyboard_focus_unchanged"] = True
finally:
    move(original_cursor["x"], original_cursor["y"])
    evaluate('if _G.arkpets_activity_test then _G.arkpets_activity_test:set_enabled(false) end')
(args.output/"report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n")
print(json.dumps(report, ensure_ascii=False, indent=2))
