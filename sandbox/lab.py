"""Headless sandbox lifecycle. No process-name kills and no normal server operations."""
import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import uuid
from pathlib import Path

from setup_runtime import MYSQL_BIN, PORT, mysql, read_config, write_config


def check(root):
    if not (root / 'runtime-ready.json').is_file() or not (root / '.wowbot-lab').is_file():
        raise RuntimeError('Sandbox is not prepared')
    world = read_config(root / 'runtime/configs/worldserver.conf')
    bots = read_config(root / 'runtime/configs/modules/playerbots.conf')
    for key, database in [('LoginDatabaseInfo', 'lab_auth'), ('CharacterDatabaseInfo', 'lab_characters'),
                          ('WorldDatabaseInfo', 'lab_world'), ('PlayerbotsDatabaseInfo', 'lab_playerbots')]:
        parts = {**world, **bots}[key].split(';')
        if len(parts) != 5 or parts[:3] != ['127.0.0.1', str(PORT), 'botlab'] or parts[4] != database:
            raise RuntimeError(f'Unsafe database configuration: {key}')
    if world['BindIP'] != '127.0.0.1' or world['WorldServerPort'] != '18085':
        raise RuntimeError('Unsafe world listener')
    for key in ('DataDir', 'LogsDir', 'PidFile'):
        path = Path(world[key]).resolve()
        if root not in path.parents:
            raise RuntimeError(f'Path escapes sandbox: {key}')
    for key in ('AiPlayerbot.RandomBotAutologin', 'AiPlayerbot.BotAutologin', 'AiPlayerbot.MinRandomBots', 'AiPlayerbot.MaxRandomBots', 'AiPlayerbot.RandomBotAccountCount', 'AiPlayerbot.AddClassAccountPoolSize', 'AiPlayerbot.AutoTeleportForLevel'):
        if bots[key] != '0':
            raise RuntimeError(f'Non-test population enabled: {key}')
    if bots['AiPlayerbot.BotCheats']:
        raise RuntimeError('Bot cheats must be disabled')
    return world


def ensure_database(root):
    try:
        result = mysql(root, 'SELECT @@datadir;', admin=False)
    except RuntimeError:
        with socket.socket() as sock:
            if sock.connect_ex(('127.0.0.1', PORT)) == 0:
                raise RuntimeError('Port occupied by an unverified database')
        process = subprocess.Popen([str(MYSQL_BIN / 'mysqld.exe'), f'--defaults-file={root / "mysql.ini"}'], creationflags=subprocess.CREATE_NO_WINDOW)
        (root / 'mysql.pid').write_text(str(process.pid))
        for _ in range(60):
            try:
                result = mysql(root, 'SELECT @@datadir;', admin=False)
                break
            except RuntimeError:
                if process.poll() is not None:
                    raise RuntimeError('Private MySQL stopped')
                time.sleep(1)
        else:
            raise RuntimeError('Private MySQL startup timeout')
    actual = Path(result.strip().splitlines()[-1]).resolve()
    if actual != (root / 'mysql-data').resolve():
        raise RuntimeError('Database datadir identity mismatch')


def supervisor(root, speed):
    world = check(root)
    # Exclusive process-held lock automatically releases on exit, including a crash.
    import msvcrt
    lock = (root / 'supervisor.lock').open('a+b')
    lock.seek(0)
    if lock.read(1) == b'':
        lock.write(b'0')
        lock.flush()
    lock.seek(0)
    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
    ensure_database(root)
    with socket.socket() as sock:
        if sock.connect_ex(('127.0.0.1', 18085)) == 0:
            raise RuntimeError('Sandbox world port already occupied')
    world['Sandbox.Speed'] = str(speed)
    write_config(root / 'runtime/configs/worldserver.conf', world)
    target = root / 'runtime/wowbot-lab-world.exe'
    shutil.copy2(root / 'build/bin/RelWithDebInfo/wowbot-lab-world.exe', target)
    inbox = root / 'commands'
    inbox.mkdir(exist_ok=True)
    # Do not replay leftover commands from an earlier run.
    for stale in inbox.glob('*.cmd'):
        stale.rename(stale.with_suffix('.stale'))
    run_id = time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6]
    output_path = root / 'logs' / f'{run_id}-console.log'
    checkpoint = root / 'clock.json'
    previous_epoch = json.loads(checkpoint.read_text())['epoch_ns'] if checkpoint.exists() else 0
    logout_epoch = int(mysql(root, 'SELECT COALESCE(MAX(logout_time),0) FROM characters;',
                             database='lab_characters', admin=False).strip().splitlines()[-1])
    epoch_ns = max(time.time_ns(), previous_epoch, (logout_epoch + 1) * 1_000_000_000)
    environment = {key: value for key, value in os.environ.items() if not key.startswith('AC_')}
    environment['WOWBOT_LAB_EPOCH_NS'] = str(epoch_ns)
    with output_path.open('wb') as output:
        process = subprocess.Popen([str(target), '-c', str(root / 'runtime/configs/worldserver.conf')],
                                   cwd=root / 'runtime', stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT,
                                   env=environment,
                                   creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        state = {'supervisor_pid': os.getpid(), 'world_pid': process.pid, 'run_id': run_id, 'speed': speed,
                 'console': str(output_path), 'start_epoch_ns': epoch_ns}
        (root / 'run.json').write_text(json.dumps(state, indent=2))
        while process.poll() is None:
            for command in sorted(inbox.glob('*.cmd')):
                content = command.read_text(encoding='utf-8')
                process.stdin.write((content.rstrip() + '\n').encode('utf-8'))
                process.stdin.flush()
                command.rename(command.with_suffix('.sent'))
            time.sleep(0.1)
        state['exit_code'] = process.returncode
        final_log = (root / 'logs/Server.log').read_text(encoding='utf-8', errors='replace')
        epochs = re.findall(r'\[SANDBOX-CLOCK\] epoch_ns=(\d+)', final_log)
        if epochs and process.returncode == 0:
            state['end_epoch_ns'] = int(epochs[-1])
            checkpoint.write_text(json.dumps({'epoch_ns': max(previous_epoch, int(epochs[-1])), 'run_id': run_id}))
        for name in ('Server.log', 'Playerbots.log', 'Errors.log'):
            log = root / 'logs' / name
            if log.exists():
                shutil.copy2(log, root / 'logs' / f'{run_id}-{name}')
        (root / 'run.json').write_text(json.dumps(state, indent=2))
    lock.close()


def is_live(pid, expected_image=None):
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        return False
    try:
        code = wintypes.DWORD()
        if not kernel.GetExitCodeProcess(handle, ctypes.byref(code)) or code.value != 259:
            return False
        if expected_image is not None:
            buffer = ctypes.create_unicode_buffer(32768)
            length = wintypes.DWORD(len(buffer))
            if not kernel.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(length)):
                return False
            if Path(buffer.value).resolve() != Path(expected_image).resolve():
                return False
        return True
    finally:
        kernel.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path('D:/wowbot-lab'))
    sub = parser.add_subparsers(dest='action', required=True)
    for name in ('start', '_supervise'):
        p = sub.add_parser(name)
        p.add_argument('--speed', type=float, default=4.0)
    sub.add_parser('status')
    sub.add_parser('stop')
    sub.add_parser('stop-db')
    p = sub.add_parser('command')
    p.add_argument('text')
    args = parser.parse_args()
    root = args.root.resolve()
    check(root)
    state = json.loads((root / 'run.json').read_text()) if (root / 'run.json').exists() else {}
    if args.action in ('start', '_supervise'):
        if not 1 <= args.speed <= 20:
            raise RuntimeError('Speed must be 1..20')
        if args.action == '_supervise':
            supervisor(root, args.speed)
        else:
            if state and is_live(state['world_pid'], root / 'runtime/wowbot-lab-world.exe'):
                raise RuntimeError('Sandbox already running')
            with (root / 'logs/supervisor.log').open('ab') as output:
                process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--root', str(root), '_supervise', '--speed', str(args.speed)],
                                           stdout=output, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            print(f'Sandbox supervisor started: PID {process.pid}. Use status and inspect its console log for readiness.')
    elif args.action == 'status':
        state['world_live'] = bool(state and is_live(state['world_pid'], root / 'runtime/wowbot-lab-world.exe'))
        state['supervisor_live'] = bool(state.get('supervisor_pid') and is_live(state['supervisor_pid'], sys.executable))
        print(json.dumps(state, indent=2))
    elif args.action in ('command', 'stop'):
        if not state or not is_live(state['world_pid'], root / 'runtime/wowbot-lab-world.exe') or not is_live(state['supervisor_pid'], sys.executable):
            raise RuntimeError('No live sandbox supervisor/world process')
        path = root / 'commands' / (str(time.time_ns()) + '.cmd')
        temp = path.with_suffix('.tmp')
        temp.write_text('server exit' if args.action == 'stop' else args.text, encoding='utf-8')
        temp.rename(path)
        print(f'Queued sandbox command: {path.name}')
    else:
        if state and is_live(state['world_pid'], root / 'runtime/wowbot-lab-world.exe'):
            raise RuntimeError('Stop sandbox world first')
        ensure_database(root)
        result = subprocess.run([str(MYSQL_BIN / 'mysqladmin.exe'), f'--defaults-file={root / "mysql-admin.cnf"}', '--host=127.0.0.1', f'--port={PORT}', '--protocol=TCP', 'shutdown'], capture_output=True)
        if result.returncode:
            raise RuntimeError(result.stderr.decode())
        print('Private sandbox database stopped.')


if __name__ == '__main__':
    main()
