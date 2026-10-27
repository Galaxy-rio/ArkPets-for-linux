#!/usr/bin/env python3
"""Opt-in startup regression on the real graphics driver, including bad locale."""
import argparse
import json
import os
from pathlib import Path
import subprocess

import gi
gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--model", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--monitor", required=True)
parser.add_argument("--binary", type=Path, default=ROOT/"bin/arkpets-renderer")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
base = {k: v for k, v in os.environ.items() if not k.startswith("LC_")
        and k not in ("GDK_DEBUG", "GDK_DISABLE", "G_DEBUG", "LIBGL_ALWAYS_SOFTWARE", "GALLIUM_DRIVER")}
base["GSETTINGS_BACKEND"] = "memory"
base["XDG_CACHE_HOME"] = str((args.output/"cache").resolve())


def evaluate(code):
    assert subprocess.check_output(["hyprctl", "eval", code], text=True).strip() == "ok"


def layers():
    return [e for m in json.loads(subprocess.check_output(["hyprctl", "-j", "layers"], text=True)).values()
            for level in m["levels"].values() for e in level if e["namespace"] == "arkpets-startup-test"]


assert not layers(), "A startup test is already running"
evaluate('if _G.arkpets_startup_test then _G.arkpets_startup_test:set_enabled(true) '
         'else _G.arkpets_startup_test=hl.layer_rule({name="arkpets-startup-test",'
         'match={namespace="^arkpets-startup-test$"},blur=false,blur_popups=false,dim_around=false,no_anim=true}) end')
results = []
try:
    for name, extra, expected in [
        ("session-locale", {}, 0),
        ("invalid-locale", {"LANG": "arkpets_INVALID.UTF-8", "G_DEBUG": "fatal-warnings"}, 0),
        ("existing-gl-debug", {"GDK_DEBUG": "opengl", "LC_ALL": "C.UTF-8"}, 0),
        ("gl-disabled", {"GDK_DISABLE": "gl", "LC_ALL": "C.UTF-8"}, 1),
    ]:
        folder = args.output/name
        folder.mkdir(exist_ok=True)
        command = [str(args.binary.resolve()), "--model", str(args.model.resolve()),
                   "--monitor", args.monitor, "--namespace", "arkpets-startup-test",
                   "--scale", "0.7", "--duration", "2", "--capture-dir", str(folder.resolve())]
        result = subprocess.run(command, env={**base, **extra}, capture_output=True, text=True, timeout=12)
        (folder/"stdout.log").write_text(result.stdout)
        (folder/"stderr.log").write_text(result.stderr)
        assert result.returncode == expected, (name, result.returncode, result.stderr)
        assert not layers()
        assert "Locale not supported" not in result.stderr, result.stderr
        record = {"case": name, "exit_code": result.returncode}
        if expected == 0:
            assert result.stdout.count("ARKPETS_READY") == 1
            assert result.stdout.index("OpenGL:") < result.stdout.index("ARKPETS_READY")
            frames = [GdkPixbuf.Pixbuf.new_from_file(str(folder/f"frame-{n}.png")) for n in (0, 1)]
            assert all(f.get_has_alpha() for f in frames)
            pixels = [bytes(f.get_pixels()) for f in frames]
            assert sum(a != b for a, b in zip(*pixels)) > 1000
            alpha = pixels[0][3::4]
            assert .3 < alpha.count(0)/len(alpha) < .98
            record["renderer"] = next(line for line in result.stdout.splitlines() if line.startswith("OpenGL:"))
            record["animated_transparent_frames"] = True
        else:
            assert "ARKPETS_READY" not in result.stdout
            assert "错误:" in result.stderr and "GL" in result.stderr
            record["no_false_ready"] = True
        results.append(record)
finally:
    evaluate('if _G.arkpets_startup_test then _G.arkpets_startup_test:set_enabled(false) end')
report = {"result": "PASS", "checks": results}
(args.output/"report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n")
print(json.dumps(report, ensure_ascii=False, indent=2))
