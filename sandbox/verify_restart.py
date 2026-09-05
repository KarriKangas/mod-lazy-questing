"""Finish a live 4x smoke run, then verify a clean 1x restart of the same roster."""
import json
import subprocess
import sys
import time
from pathlib import Path
from lab import is_live

ROOT = Path('D:/wowbot-lab')
HERE = Path(__file__).resolve().parent


def state():
    return json.loads((ROOT / 'run.json').read_text())


def command(*arguments):
    subprocess.run([sys.executable, str(HERE / 'lab.py'), *arguments], check=True)


def wait_for_samples(run, count, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        current = state()
        if current['run_id'] != run['run_id']:
            raise RuntimeError('Run changed unexpectedly; refusing to follow a replacement')
        if not is_live(run['world_pid'], ROOT / 'runtime/wowbot-lab-world.exe'):
            raise RuntimeError('World stopped before verification completed')
        text = (ROOT / 'logs/Server.log').read_text(encoding='utf-8', errors='replace')
        if text.count('[SANDBOX-PROBE] online=8 ') >= count:
            return
        time.sleep(1)
    raise RuntimeError('Sample observation timeout; world was not restarted or killed')


def stop_and_capture(label):
    command('stop')
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        run = state()
        if 'exit_code' in run and not is_live(run['world_pid'], ROOT / 'runtime/wowbot-lab-world.exe'):
            if run['exit_code'] != 0:
                raise RuntimeError('World shutdown failed')
            subprocess.run([sys.executable, str(HERE / 'capture_smoke.py'), '--label', label], check=True)
            return run
        time.sleep(1)
    raise RuntimeError('Shutdown observation timeout; inspect the same process before further action')


def main():
    run = state()
    if run['speed'] != 4:
        raise RuntimeError('Start the final binary at 4x first')
    print('Observing current 4x run for at least four complete probe samples.', flush=True)
    wait_for_samples(run, 4, 180)
    previous = stop_and_capture('final-4x')
    checkpoint = previous['end_epoch_ns']
    print('4x run captured; checking persisted epoch during 1x restart.', flush=True)
    command('start', '--speed', '1')
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        next_run = state()
        if next_run['run_id'] != previous['run_id']:
            break
        time.sleep(0.2)
    else:
        raise RuntimeError('New supervisor did not publish a run')
    if next_run['start_epoch_ns'] < checkpoint:
        raise RuntimeError('Persisted epoch moved backwards')
    # Wait for the new world to initialize; do not read the preceding run's old log.
    ready = time.monotonic() + 120
    while time.monotonic() < ready:
        if not is_live(next_run['world_pid'], ROOT / 'runtime/wowbot-lab-world.exe'):
            raise RuntimeError('Restarted world stopped during startup')
        text = (ROOT / 'logs/Server.log').read_text(encoding='utf-8', errors='replace')
        if '[SANDBOX] speed=1 fixed_step_ms=50' in text:
            break
        time.sleep(1)
    else:
        raise RuntimeError('Restart readiness timeout')
    wait_for_samples(next_run, 3, 240)
    final = stop_and_capture('final-1x-restart')
    result = json.loads((ROOT / 'validation/final-1x-restart/result.json').read_text())
    if result['probe_samples'][0]['game_s'] * 1_000_000_000 < checkpoint:
        raise RuntimeError('Actual game clock moved behind previous run')
    (ROOT / 'validation/restart.json').write_text(json.dumps({
        'previous_run_id': previous['run_id'], 'previous_end_epoch_ns': checkpoint,
        'restart_run_id': final['run_id'], 'restart_start_epoch_ns': final['start_epoch_ns'],
        'restart_end_epoch_ns': final['end_epoch_ns'], 'passed': True}, indent=2))
    print('PASS: final binary completed 4x and 1x runs, with persistent epoch and legal gameplay activity.', flush=True)


if __name__ == '__main__':
    main()
