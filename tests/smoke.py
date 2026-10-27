#!/usr/bin/env python3
"""Check real layer-shell rendering with a separately downloaded Spine model."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time

import gi
gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--model", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--monitor", default="")
opts = parser.parse_args()
opts.output.mkdir(parents=True, exist_ok=True)


def hypr(command):
    return json.loads(subprocess.check_output(["hyprctl", "-j", command], text=True))


def pets():
    return [dict(monitor=m, level=int(level), **entry)
            for m, monitor in hypr("layers").items()
            for level, entries in monitor["levels"].items()
            for entry in entries if entry["namespace"] == "arkpets-hyprland"]


def png(path):
    pix = GdkPixbuf.Pixbuf.new_from_file(str(path))
    w, h, stride = pix.get_width(), pix.get_height(), pix.get_rowstride()
    assert pix.get_n_channels() == 4 and pix.get_has_alpha()
    source = pix.get_pixels()
    return w, h, b"".join(source[y*stride:y*stride+w*4] for y in range(h))


assert not pets(), "Stop the existing pet before testing"
binary = str(ROOT / "bin/arkpets-renderer")
base = [binary, "--model", str(opts.model.resolve())]
animations = subprocess.check_output(base + ["--list-animations"], text=True)
assert "Relax" in animations
for invalid in [["--animation", "does-not-exist"], ["--fps", "0"],
                ["--scale", "nan"], ["--duration", "nan"]]:
    result = subprocess.run(base + invalid, capture_output=True, text=True)
    assert result.returncode != 0 and "错误:" in result.stderr

focus = hypr("activewindow").get("address")
results = []
for click_through in (False, True):
    folder = opts.output / ("click-through" if click_through else "draggable")
    folder.mkdir(exist_ok=True)
    command = base + ["--scale", "0.64", "--duration", "2", "--capture-dir", str(folder.resolve())]
    if opts.monitor:
        command += ["--monitor", opts.monitor]
    if click_through:
        command += ["--click-through"]
    with (folder / "wayland.log").open("w") as log:
        child = subprocess.Popen(command, stdout=log, stderr=log,
                                 env={**os.environ, "WAYLAND_DEBUG": "client", "GSETTINGS_BACKEND": "memory"})
        try:
            deadline = time.monotonic() + 4
            surface = []
            while child.poll() is None and time.monotonic() < deadline:
                surface = pets()
                if surface and "ARKPETS_READY" in (folder / "wayland.log").read_text() and surface[0]["alpha"] == 1:
                    break
                time.sleep(.05)
            assert len(surface) == 1 and surface[0]["level"] == 2, surface
            if opts.monitor:
                assert surface[0]["monitor"] == opts.monitor
            assert not any(c["pid"] == surface[0]["pid"] for c in hypr("clients"))
            assert hypr("activewindow").get("address") == focus, "Pet stole keyboard focus"
            assert child.wait(timeout=8) == 0
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=5)
    assert not pets(), "Layer survived process exit"
    log = (folder / "wayland.log").read_text()
    assert "set_keyboard_interactivity(0)" in log
    assert "set_exclusive_zone(-1)" in log
    regions, applied = {}, []
    for line in log.splitlines():
        created = re.search(r"create_region\(new id wl_region#(\d+)\)", line)
        added = re.search(r"wl_region#(\d+)\.add\((\d+), (\d+), (\d+), (\d+)\)", line)
        used = re.search(r"set_input_region\(wl_region#(\d+)\)", line)
        if created:
            regions[created[1]] = 0
        if added:
            regions[added[1]] = regions.get(added[1], 0) + int(added[4])*int(added[5])
        if used:
            applied.append(regions.get(used[1], 0))
    assert applied
    assert max(applied) == 0 if click_through else 0 < max(applied) < surface[0]["w"]*surface[0]["h"]
    w, h, first = png(folder / "frame-0.png")
    w2, h2, second = png(folder / "frame-1.png")
    assert (w, h) == (w2, h2)
    alpha = first[3::4]
    fraction = alpha.count(0) / len(alpha)
    assert .3 < fraction < .98 and any(0 < a < 255 for a in alpha)
    assert all(alpha[y*w+x] == 0 for y in (0, h-1) for x in range(w))
    assert all(alpha[y*w+x] == 0 for y in range(h) for x in (0, w-1))
    changed = sum(a != b for a, b in zip(first, second))
    assert changed > 1000, "Animation did not advance"
    results.append({"click_through": click_through, "layer": surface[0], "capture_pixels": [w, h],
                    "transparent_fraction": fraction, "changed_bytes": changed,
                    "maximum_input_area": max(applied), "keyboard_focus_unchanged": True})
report = {"result": "PASS", "checks": results}
(opts.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n")
print(json.dumps(report, ensure_ascii=False, indent=2))
