"""Capture a completed smoke run; never use these checks as an efficacy comparison."""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path
from lab import check, is_live
from setup_runtime import mysql


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path('D:/wowbot-lab'))
    parser.add_argument('--label', required=True)
    parser.add_argument('--bots', type=int, default=8)
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z0-9-]+', args.label):
        raise RuntimeError('Label must contain only lowercase letters, digits, and hyphens')
    root = args.root.resolve()
    check(root)
    state = json.loads((root / 'run.json').read_text())
    if is_live(state['world_pid'], root / 'runtime/wowbot-lab-world.exe') or state.get('exit_code') != 0:
        raise RuntimeError('Capture requires a successful graceful shutdown')
    output = root / 'validation' / args.label
    output.mkdir(parents=True, exist_ok=False)
    for name in ('Server.log', 'Playerbots.log', 'Errors.log'):
        source = root / 'logs' / f'{state["run_id"]}-{name}'
        if not source.exists():
            source = root / 'logs' / name
        shutil.copy2(source, output / name)
    shutil.copy2(state['console'], output / 'console.log')
    log = (output / 'Server.log').read_text(encoding='utf-8', errors='replace')
    ticks = []
    probes = []
    for line in log.splitlines():
        if '[SANDBOX] ticks=' in line:
            ticks.append({k: float(v) for k, v in re.findall(r'(\w+)=([\d.eE+-]+)', line)})
        if '[SANDBOX-PROBE]' in line:
            probes.append({k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)})
    if len(ticks) < 3 or len(probes) < 3:
        raise RuntimeError('Need at least three clock and probe samples')
    if any(row['step_ms'] != 50 or row['requested_speed'] != state['speed'] for row in ticks):
        raise RuntimeError('Unexpected step or requested speed')
    if any(row['online'] != args.bots or row['game_s'] != row['unix_s'] for row in probes):
        raise RuntimeError('Roster or clock disagreement')
    for before, after in zip(probes, probes[1:]):
        if after['mono_ms'] - before['mono_ms'] != 60000 or after['game_s'] - before['game_s'] != 60:
            raise RuntimeError('Probe interval clock mismatch')
        if abs(after['total_played_s'] - before['total_played_s'] - args.bots * 60) > args.bots:
            raise RuntimeError('Played time did not track simulation time')
    speed = (ticks[-1]['simulation_ms'] - ticks[0]['simulation_ms']) / (ticks[-1]['wall_ms'] - ticks[0]['wall_ms'])
    if not (0.9 * state['speed'] <= speed <= 1.1 * state['speed']):
        raise RuntimeError(f'Achieved speed {speed} is outside this small-roster smoke tolerance')
    if probes[-1]['xp'] <= 0 or probes[-1]['kills'] <= 0:
        raise RuntimeError('No ordinary XP and kill activity observed')
    endpoint = mysql(root, 'SELECT guid,name,race,class,level,xp,totaltime,online,map,position_x,position_y,position_z FROM characters ORDER BY guid;', database='lab_characters', admin=False)
    (output / 'characters.tsv').write_text(endpoint)
    non_strict = mysql(root, 'SELECT COUNT(*) FROM characters c LEFT JOIN strict_altbots s ON s.character_guid=c.guid WHERE s.character_guid IS NULL;', database='lab_characters', admin=False).strip().splitlines()[-1]
    if non_strict != '0':
        raise RuntimeError('Non-strict characters found')
    data = {'run': state, 'achieved_speed': speed, 'clock_samples': len(ticks), 'probe_samples': probes,
            'non_strict_characters': int(non_strict), 'binary_sha256': hashlib.file_digest((root / 'runtime/wowbot-lab-world.exe').open('rb'), 'sha256').hexdigest()}
    (output / 'result.json').write_text(json.dumps(data, indent=2))
    print(json.dumps({'label': args.label, 'achieved_speed': speed, 'clock_samples': len(ticks), 'last_probe': probes[-1], 'non_strict_characters': 0}, indent=2))


if __name__ == '__main__':
    main()
