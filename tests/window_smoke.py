#!/usr/bin/env python3
"""Opt-in real Hyprland drag/resize/hide/tray/restore test, in a private profile."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, default=ROOT/'bin/arkpets')
parser.add_argument('--profile', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--pointer', type=Path, default=ROOT/'bin/drag-probe')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
args.profile.mkdir(parents=True, exist_ok=True)
(args.output/'settings-window.png').unlink(missing_ok=True)
command = [str(args.binary.resolve()), '--data-dir', str((args.profile/'data').resolve()),
           '--config-dir', str((args.profile/'config').resolve())]


def hypr(name):
    return json.loads(subprocess.check_output(['hyprctl', '-j', name], text=True))


def eventually(predicate, timeout=8):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = predicate()
        if value:
            return value
        time.sleep(.08)
    raise AssertionError('Timed out waiting for desktop state')


def move(x, y):
    result = subprocess.check_output(['hyprctl', 'eval',
        f'hl.dispatch(hl.dsp.cursor.move({{x={int(x)},y={int(y)}}}))'], text=True)
    assert result.strip() == 'ok', result
    time.sleep(.15)


def drag(dx, dy):
    subprocess.run([str(args.pointer.resolve()), str(dx), str(dy)], check=True, timeout=4)
    time.sleep(.4)


cursor = hypr('cursorpos')
log = (args.output/'window.log').open('w')
process = subprocess.Popen(command + ['--page', 'settings', '--screenshot',
    str((args.output/'settings-window.png').resolve())], stdout=log, stderr=log)


def client():
    return next((c for c in hypr('clients') if c['pid'] == process.pid), None)


try:
    eventually(lambda: (client() or {}).get("visible", False))
    initial = client()
    assert initial['floating'], 'This test requires a floating ArkPets window'
    eventually(lambda: (args.output/'settings-window.png').exists())
    move(initial['at'][0] + initial['size'][0]//2, initial['at'][1] + 32)
    drag(-100, 60)
    moved = client()
    assert moved and moved['at'][0] < initial['at'][0] - 40, (initial, moved)
    assert moved['at'][1] > initial['at'][1] + 20, (initial, moved)
    move(moved['at'][0] + moved['size'][0] - 3, moved['at'][1] + moved['size'][1] - 3)
    drag(80, 48)
    resized = client()
    assert resized['size'][0] > moved['size'][0] + 30, (moved, resized)
    assert resized['size'][1] > moved['size'][1] + 15, (moved, resized)

    def close_button():
        eventually(lambda: (client() or {}).get("visible", False))
        time.sleep(.5)  # Hyprland open animation after the surface is remapped.
        window = client()
        move(window['at'][0] + window['size'][0] - 36, window['at'][1] + 32)
        drag(0, 0)
        eventually(lambda: client() is None)
        assert process.poll() is None, 'Close unexpectedly quit the manager'

    close_button()
    entries = json.loads(subprocess.check_output(['busctl', '--user', '--json=short', 'get-property',
        'org.kde.StatusNotifierWatcher', '/StatusNotifierWatcher',
        'org.kde.StatusNotifierWatcher', 'RegisteredStatusNotifierItems'], text=True))['data']
    service = item_path = None
    for entry in entries:
        name, _, suffix = entry.partition('/')
        owner = subprocess.check_output(['busctl', '--user', 'call', 'org.freedesktop.DBus',
            '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'GetConnectionUnixProcessID', 's', name], text=True)
        if int(owner.split()[-1]) == process.pid:
            service, item_path = name, '/' + suffix
            break
    assert service, 'Test app did not register with the tray host'
    details = subprocess.check_output(['busctl', '--user', 'introspect', service, item_path], text=True)
    assert '.Activate' in details
    subprocess.run(['busctl', '--user', 'call', service, item_path,
        'org.kde.StatusNotifierItem', 'Activate', 'ii', '0', '0'], check=True, timeout=5)
    eventually(lambda: (client() or {}).get("visible", False))
    close_button()
    subprocess.run(command, check=True, timeout=5)
    eventually(lambda: (client() or {}).get("visible", False))
    subprocess.run(command + ['--quit'], check=True, timeout=5)
    assert process.wait(timeout=5) == 0
    assert client() is None
    report = {'result': 'PASS', 'initial': initial, 'moved': moved, 'resized': resized,
              'close_hides_without_quitting': True, 'tray_activation_restores': True,
              'second_launch_restores': True, 'explicit_quit_exits': True}
    (args.output/'window-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps({key: value for key, value in report.items() if key not in ('initial', 'moved', 'resized')}))
finally:
    if process.poll() is None:
        subprocess.run(command + ['--quit'], timeout=5)
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=4)
    log.close()
    move(cursor['x'], cursor['y'])
