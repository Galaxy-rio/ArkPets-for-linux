#!/usr/bin/env python3
"""Opt-in native Hyprland regression: raw model scale, click, replay and drag.

Uses a separate namespace and an already downloaded Shu. Never stops the user's
pet, downloads assets or changes their settings. Temporarily moves the pointer.
"""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import subprocess
import time

import gi
gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf, GLib

ROOT = Path(__file__).resolve().parents[1]
NS = "arkpets-renderer-qa"
parser = argparse.ArgumentParser()
parser.add_argument("--model", type=Path, required=True)
parser.add_argument("--special-model", type=Path, help="Optional Warfarin model with Interact and Special")
parser.add_argument("--comparison-model", type=Path, action="append", default=[], help="Additional models that must use the same raw scale")
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--monitor", default="DP-4")
parser.add_argument("--binary", type=Path, default=ROOT/"bin/arkpets-renderer")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)


def hypr(command):
    return json.loads(subprocess.check_output(["hyprctl", "-j", command], text=True))


def evaluate(code):
    result = subprocess.check_output(["hyprctl", "eval", code], text=True).strip()
    assert result == "ok", result


def surface():
    entries = [e for m in hypr("layers").values() for level in m["levels"].values()
               for e in level if e["namespace"] == NS]
    assert len(entries) <= 1
    return entries[0] if entries else None


def wait_for(condition, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = condition()
        if value:
            return value
        time.sleep(.04)
    raise AssertionError("Timed out waiting for renderer state")


def png(path):
    pix = GdkPixbuf.Pixbuf.new_from_file(str(path))
    w, h, stride = pix.get_width(), pix.get_height(), pix.get_rowstride()
    assert pix.get_n_channels() == 4
    raw = pix.get_pixels()
    return w, h, b"".join(raw[y*stride:y*stride+w*4] for y in range(h))


def capture_ready(path):
    try:
        png(path)
        return True
    except GLib.Error:
        return False


def silhouette(path):
    w, h, rgba = png(path)
    alpha = rgba[3::4]
    assert all(alpha[y*w+x] == 0 for y in (0, h-1) for x in range(w)), "Vertical clipping"
    assert all(alpha[y*w+x] == 0 for x in (0, w-1) for y in range(h)), "Horizontal clipping"
    rows = [y for y in range(h) if any(a > 20 for a in alpha[y*w:(y+1)*w])]
    assert rows
    return rows[-1]-rows[0]+1


def point_at_pet(image, layer, mirrored=False, vertical=.4):
    w, h, rgba = png(image)
    alpha = rgba[3::4]
    candidates = []
    for y in range(24, h-24, 6):
        for x in range(24, w-24, 6):
            if all(alpha[yy*w+xx] > 230 and
                   (not mirrored or alpha[yy*w+w-1-xx] > 230)
                   for yy in range(y-18, y+19, 6) for xx in range(x-18, x+19, 6)):
                candidates.append((abs(x-w/2)+abs(y-h*vertical), x, y))
    assert candidates, "No opaque interior for the test pointer"
    _, x, y = min(candidates)
    move(layer["x"]+x*layer["w"]/w, layer["y"]+y*layer["h"]/h)


def move(x, y):
    evaluate(f"hl.dispatch(hl.dsp.cursor.move({{x={int(x)},y={int(y)}}}))")
    time.sleep(.08)


def layout(log):
    line = next(line for line in log.splitlines() if line.startswith("ARKPETS_LAYOUT "))
    scale, width, canvas_height = line.split(" ", 1)[1].split("\t")
    return {"scale": float(scale), "canvas": [int(width), int(canvas_height)]}


@contextmanager
def renderer(name, *extra, wayland_debug=False, model=None):
    folder = args.output/name
    folder.mkdir(exist_ok=True)
    logfile = folder/"renderer.log"
    for image in folder.glob("frame-*.png"):
        image.unlink()
    with logfile.open("w") as log:
        command = [str(args.binary.resolve()), "--model", str((model or args.model).resolve()),
                   "--namespace", NS, "--monitor", args.monitor, "--scale", "0.75",
                   "--right", "1000", "--bottom", "500", "--layer", "overlay",
                   "--duration", "25", "--capture-dir", str(folder.resolve()), *extra]
        environment = {**os.environ, "GSETTINGS_BACKEND": "memory"}
        if wayland_debug:
            environment["WAYLAND_DEBUG"] = "client"
        child = subprocess.Popen(command, stdout=log, stderr=log, env=environment)
        try:
            wait_for(lambda: "ARKPETS_READY" in logfile.read_text() or child.poll() is not None)
            assert child.poll() is None, logfile.read_text()
            wait_for(lambda: capture_ready(folder/"frame-1.png"))
            assert surface()["pid"] == child.pid
            yield folder, logfile.read_text
        finally:
            if child.poll() is None:
                child.terminate()
            assert child.wait(timeout=5) == 0, logfile.read_text()
    wait_for(lambda: surface() is None)
    assert "错误:" not in logfile.read_text(), logfile.read_text()


assert surface() is None, "A renderer QA process is already running"
original_cursor = hypr("cursorpos")
focus = hypr("activewindow").get("address")
report = {"monitor": args.monitor, "scaling": {}}
evaluate('if _G.arkpets_renderer_qa then _G.arkpets_renderer_qa:set_enabled(true) '
         'else _G.arkpets_renderer_qa=hl.layer_rule({name="arkpets-renderer-qa",'
         'match={namespace="^arkpets-renderer-qa$"},blur=false,blur_popups=false,dim_around=false,no_anim=true}) end')
try:
    for name, flags in (("relax", ()), ("automatic", ("--auto-activity", "--flip", "--activity-seed", "0")),
                        ("sit", ("--animation", "Sit")), ("sleep", ("--animation", "Sleep")),
                        ("interact", ("--animation", "Interact")), ("half", ("--scale", "0.375"))):
        with renderer(name, *flags) as (folder, read):
            data = layout(read())
            assert data["scale"] == (.375 if name == "half" else .75)
            assert data["canvas"] == [surface()["w"], surface()["h"]]
            data["silhouette_pixels"] = [silhouette(folder/f"frame-{n}.png") for n in (0, 1)]
            report["scaling"][name] = data
    baseline = report["scaling"]["relax"]
    for name in ("automatic", "sit", "sleep", "interact"):
        data = report["scaling"][name]
        assert data["scale"] == baseline["scale"] and data["canvas"] == baseline["canvas"]
    assert png(args.output/"relax/frame-0.png") == png(args.output/"automatic/frame-0.png"), "Automatic mode changed Relax size"
    assert abs(baseline["silhouette_pixels"][0]-2*report["scaling"]["half"]["silhouette_pixels"][0]) <= 2
    report["model_comparison"] = []
    for index, model in enumerate(args.comparison_model):
        with renderer(f"comparison-{index}", model=model) as (folder, read):
            data = layout(read())
            assert data["scale"] == baseline["scale"], "A different model changed the raw scale"
            data["model"] = model.name
            data["silhouette_pixels"] = [silhouette(folder/f"frame-{n}.png") for n in (0, 1)]
            report["model_comparison"].append(data)

    report["input_regions"] = []
    for transparent in (False, True):
        flags = ("--click-through",) if transparent else ()
        with renderer("input-through" if transparent else "input-visible", *flags, wayland_debug=True) as (_, read):
            regions, applied = {}, []
            for line in read().splitlines():
                created = re.search(r"create_region\(new id wl_region#(\d+)\)", line)
                added = re.search(r"wl_region#(\d+)\.add\((\d+), (\d+), (\d+), (\d+)\)", line)
                used = re.search(r"set_input_region\(wl_region#(\d+)\)", line)
                if created:
                    regions[created[1]] = 0
                if added:
                    regions[added[1]] = regions.get(added[1], 0) + int(added[4])*int(added[5])
                    assert int(added[2])+int(added[4]) <= baseline["canvas"][0]
                    assert int(added[3])+int(added[5]) <= baseline["canvas"][1]
                if used:
                    applied.append(regions.get(used[1], 0))
            assert applied
            assert max(applied) == 0 if transparent else 0 < max(applied) < baseline["canvas"][0]*baseline["canvas"][1]
            report["input_regions"].append({"click_through": transparent, "maximum_input_area": max(applied)})

    with renderer("screen-edge", "--right", "20000", "--bottom", "20000"):
        monitor = next(m for m in hypr("monitors") if m["name"] == args.monitor)
        edge = surface()
        assert edge["x"] <= monitor["x"] and edge["y"] <= monitor["y"]
        report["screen_edge"] = {"effect_canvas_can_extend_offscreen": True, "canvas": [edge["w"], edge["h"]]}

    with renderer("manual-click", "--animation", "Sit") as (folder, read):
        before = surface()
        point_at_pet(folder/"frame-1.png", before)
        click = subprocess.Popen([str(ROOT/"bin/drag-probe"), "--click", "200"])
        try:
            wait_for(lambda: "\tstarted" in read(), 1)
            assert click.poll() is None, "Interaction waited for button release"
        finally:
            assert click.wait(timeout=4) == 0
        assert "\tfinished" not in read(), "Release cancelled the interaction"
        # A second press restarts the one-shot instead of being ignored by the
        # current-animation-name guard.
        # Keep the replay near the sampled half-second pose and aim at the
        # torso, so a nodding head does not move out from under the pointer.
        point_at_pet(args.output/"interact/frame-1.png", surface(), vertical=.55)
        subprocess.run([str(ROOT/"bin/drag-probe"), "--click", "40"], check=True, timeout=4)
        restart = time.monotonic()
        wait_for(lambda: read().count("\tstarted") == 2, 1)
        wait_for(lambda: "\tfinished\tSit" in read(), 7)
        assert time.monotonic()-restart > 4, "Repeated click did not restart the five-second animation"
        after = surface()
        assert all(before[k] == after[k] for k in ("x", "y", "w", "h"))
        point_at_pet(folder/"frame-1.png", after)
        subprocess.run([str(ROOT/"bin/drag-probe"), "-90", "-60"], check=True, timeout=4)
        dragged = surface()
        assert abs(dragged["x"]-after["x"]+90) <= 2 and abs(dragged["y"]-after["y"]+60) <= 2
        assert read().count("\tstarted") == 3, "Dragging did not preserve press-to-interact"
        report["manual_click"] = {"press_triggered": True, "replay": True, "restored": "Sit",
                                  "drag_delta": [dragged["x"]-after["x"], dragged["y"]-after["y"]]}

    with renderer("automatic-click", "--auto-activity", "--activity-seed", "0") as (folder, read):
        wait_for(lambda: "\twalking" in read(), 6)
        point_at_pet(folder/"frame-1.png", surface(), mirrored=True)
        subprocess.run([str(ROOT/"bin/drag-probe"), "--click", "100"], check=True, timeout=4)
        wait_for(lambda: "\tstarted" in read(), 1)
        held = surface()
        time.sleep(.8)
        assert surface()["x"] == held["x"], "Automatic movement continued during interaction"
        wait_for(lambda: "\tfinished\tRelax" in read(), 7)
        wait_for(lambda: read().count("\twalking") >= 2, 6)
        wait_for(lambda: abs(surface()["x"]-held["x"]) > 20, 3)
        assert [surface()["w"], surface()["h"]] == baseline["canvas"]
        report["automatic_click"] = {"paused_for_interaction": True, "restored": "Relax", "walking_resumed": True}

    if args.special_model:
        with renderer("special-loop", "--animation", "Special", model=args.special_model) as (folder, read):
            special_layout = layout(read())
            assert special_layout["scale"] == baseline["scale"]
            for n in (0, 1):
                silhouette(folder/f"frame-{n}.png")
        # Seed 7 selects Special on the first press and Interact on the second.
        with renderer("special-manual", "--animation", "Sit", "--interaction-seed", "7",
                      "--duration", "40", model=args.special_model) as (folder, read):
            assert layout(read()) == special_layout, "Special changed the scale or canvas size"
            before = surface()
            point_at_pet(folder/"frame-1.png", before)
            subprocess.run([str(ROOT/"bin/drag-probe"), "--click", "40"], check=True, timeout=4)
            wait_for(lambda: "ARKPETS_INTERACTION Special\tstarted" in read(), 1)
            wait_for(lambda: "ARKPETS_INTERACTION Special\tfinished\tSit" in read(), 12)
            assert all(surface()[k] == before[k] for k in ("x", "y", "w", "h"))
            time.sleep(.3)
            point_at_pet(folder/"frame-1.png", surface())
            subprocess.run([str(ROOT/"bin/drag-probe"), "--click", "40"], check=True, timeout=4)
            wait_for(lambda: "ARKPETS_INTERACTION Interact\tstarted" in read(), 1)
            wait_for(lambda: "ARKPETS_INTERACTION Interact\tfinished\tSit" in read(), 3)
            report["special_manual"] = {"special_then_interact": True, "restored": "Sit",
                                        "stable_scale_and_canvas": True, "layout": special_layout}
        with renderer("special-automatic", "--auto-activity", "--activity-seed", "0",
                      "--interaction-seed", "7", model=args.special_model) as (folder, read):
            assert layout(read()) == special_layout
            point_at_pet(folder/"frame-1.png", surface())
            subprocess.run([str(ROOT/"bin/drag-probe"), "--click", "40"], check=True, timeout=4)
            wait_for(lambda: "ARKPETS_INTERACTION Special\tstarted" in read(), 1)
            held = surface()
            time.sleep(.8)
            assert surface()["x"] == held["x"], "Movement continued during Special"
            wait_for(lambda: "ARKPETS_INTERACTION Special\tfinished\tRelax" in read(), 12)
            wait_for(lambda: "\twalking" in read(), 6)
            wait_for(lambda: abs(surface()["x"]-held["x"]) > 20, 3)
            report["special_automatic"] = {"paused_for_special": True, "restored": "Relax", "walking_resumed": True}
    assert hypr("activewindow").get("address") == focus, "Pet stole keyboard focus"
    report["result"] = "PASS"
finally:
    move(original_cursor["x"], original_cursor["y"])
    evaluate('if _G.arkpets_renderer_qa then _G.arkpets_renderer_qa:set_enabled(false) end')
    (args.output/"report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n")
print(json.dumps(report, ensure_ascii=False, indent=2))
