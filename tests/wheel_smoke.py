#!/usr/bin/env python3
"""Opt-in native Wayland wheel delivery to an isolated ArkPets profile."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--binary', type=Path, default=ROOT/'bin/arkpets')
p.add_argument('--profile', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
(args.profile/'data').mkdir(parents=True, exist_ok=True)
(args.profile/'config').mkdir(parents=True, exist_ok=True)
catalog = json.loads((ROOT/'tests/fixtures/catalog.json').read_text())
entry = catalog['data']['2025_shu']
catalog['data'] = {f'wheel_{i}': dict(entry, name=f'角色 {i:03}') for i in range(600)}
(args.profile/'data/catalog.json').write_text(json.dumps(catalog))
# A pre-upgrade speed must no longer override the fixed one-row wheel step.
(args.profile/'config/panel.ini').write_text('[General]\nmode=dark\npalette=matugen\nwheelRows=12\n')
command = [str(args.binary.resolve()), '--data-dir', str((args.profile/'data').resolve()),
           '--config-dir', str((args.profile/'config').resolve())]
log_path = args.output/'native-wheel.log'
log = log_path.open('w')
env = dict(os.environ, ARKPETS_TRACE_SCROLL='1', QT_FORCE_STDERR_LOGGING='1')
env.pop('XDG_CACHE_HOME', None)  # Read this desktop's existing system palette.
process = subprocess.Popen(command+['--screenshot',str((args.output/'native-library.png').resolve())], env=env, stdout=log, stderr=log)

def hypr(name):
    return json.loads(subprocess.check_output(['hyprctl', '-j', name], text=True))

def move(x, y):
    subprocess.run(['hyprctl', 'eval', f'hl.dispatch(hl.dsp.cursor.move({{x={int(x)},y={int(y)}}}))'],
                   check=True, stdout=subprocess.DEVNULL)

cursor = hypr('cursorpos')
try:
    window = None
    for _ in range(60):
        window = next((c for c in hypr('clients') if c['pid']==process.pid and c.get('visible')), None)
        if window:
            break
        if process.poll() is not None:
            raise RuntimeError(log_path.read_text())
        time.sleep(.1)
    assert window, 'Test window did not appear'
    (args.output/'native-wheel-window.json').write_text(json.dumps(window, indent=2))
    time.sleep(1.5)
    positions = []
    for _ in range(3):
        move(window['at'][0]+480, window['at'][1]+420)
        time.sleep(.1)
        positions.append(hypr('cursorpos'))
        subprocess.run([str(ROOT/'bin/drag-probe'), '--wheel', '1'], check=True, timeout=4)
        time.sleep(.2)
    (args.output/'native-wheel-cursor.json').write_text(json.dumps(positions, indent=2))
    records = re.findall(r'ArkPets wheel .* position ([\d.-]+) -> ([\d.-]+)', log_path.read_text())
    assert len(records)==3, (records, log_path.read_text())
    deltas = [float(after)-float(before) for before, after in records]
    assert all(abs(delta-88)<1 for delta in deltas), deltas
    report = {'result':'PASS', 'input_method':'Wayland virtual pointer axis_discrete via Hyprland',
              'wheel_deltas_px':deltas, 'rows_per_notch':1, 'legacy_wheel_rows_ignored':12, 'model_count':600,
              'reported_device_touchpad':'TouchPad' in log_path.read_text()}
    (args.output/'native-wheel-report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))
finally:
    subprocess.run(command+['--quit'], env=env, timeout=5, check=False)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=5)
    log.close()
    move(cursor['x'], cursor['y'])
