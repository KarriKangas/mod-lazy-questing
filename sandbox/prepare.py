"""Create an isolated source snapshot; never modify the play server checkout."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIVE = Path('D:/wowserver')
CORE = LIVE / 'azerothcore-wotlk'
MODULES = ['mod-playerbots', 'mod-strict-altbot-guild', 'mod-lazy-questing']


def digest(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()


def source_hashes(source):
    # Revision IDs alone do not capture local, uncommitted dependency changes.
    return {str(path.relative_to(source)): digest(path)
            for base in ('src', 'modules', 'deps')
            for path in sorted((source / base).rglob('*'))
            if path.is_file() and (path.suffix in ('.cpp', '.h', '.cmake') or path.name == 'CMakeLists.txt')}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path('D:/wowbot-lab'))
    args = parser.parse_args()
    root = args.root.resolve()
    if root == LIVE or LIVE in root.parents or root in LIVE.parents:
        raise RuntimeError('Sandbox must be outside D:/wowserver')
    if root.exists() and not (root / '.wowbot-lab').exists():
        raise RuntimeError('Refusing to overwrite an unmarked directory')
    if (root / 'source-manifest.json').exists():
        raise RuntimeError('Source already prepared; refusing to reapply overlay')
    root.mkdir(parents=True, exist_ok=True)
    baseline_paths = [LIVE / 'StartServer.bat', LIVE / 'WorldserverLoop.bat']
    baseline_paths += [p for p in (Path('D:/Games/wow/Wow.exe'), Path('D:/Games/wow/Data/enUS/realmlist.wtf')) if p.exists()]
    binary = LIVE / 'build/bin/RelWithDebInfo'
    baseline_paths += list(binary.glob('*.exe')) + list((binary / 'configs').rglob('*.conf'))
    baseline = {str(p): digest(p) for p in baseline_paths}
    if not (root / 'live-baseline.json').exists():
        (root / 'live-baseline.json').write_text(json.dumps(baseline, indent=2))
    (root / '.wowbot-lab').write_text('isolated accelerated bot sandbox\n')
    source = root / 'source'
    def ignore(directory, names):
        omitted = {'.git', '.vs', '__pycache__', '.agents', '.codex', '.claude', 'out'}
        if Path(directory) == CORE:
            omitted.add('modules')
        return set(names) & omitted
    shutil.copytree(CORE, source, ignore=ignore, dirs_exist_ok=True)
    # Retain module build infrastructure, then copy only selected module source snapshots.
    (source / 'modules').mkdir(exist_ok=True)
    for entry in (CORE / 'modules').iterdir():
        if entry.is_file():
            shutil.copy2(entry, source / 'modules' / entry.name)
    revisions = {'core': subprocess.check_output(['git', '-C', str(CORE), 'rev-parse', 'HEAD'], text=True).strip()}
    for name in MODULES:
        origin = CORE / 'modules' / name
        shutil.copytree(origin, source / 'modules' / name, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('.git', '.agents', '.codex', 'archive', 'sandbox', '__pycache__'))
        revisions[name] = subprocess.check_output(['git', '-c', f'safe.directory={origin.as_posix()}', '-C', str(origin), 'rev-parse', 'HEAD'], text=True).strip()
    utilities = source / 'src/common/Utilities'
    shutil.copy2(HERE / 'SandboxClock.h', utilities / 'SandboxClock.h')
    scope = [source / 'src/server/game', source / 'src/server/scripts', source / 'modules']
    files = set()
    for directory in scope:
        files.update(p for p in directory.rglob('*') if p.suffix in ('.cpp', '.h'))
    files.update(utilities / n for n in ('Timer.h', 'TaskScheduler.h', 'TaskScheduler.cpp'))
    changes = {}
    for path in sorted(files):
        old = path.read_text(encoding='utf-8-sig')
        new = re.sub(r'(?<![\w:])(?:std::chrono::)?steady_clock::now\(\)', 'SandboxClock::SteadyNow()', old)
        new = re.sub(r'(?<![\w:])(?:std::chrono::)?system_clock::now\(\)', 'SandboxClock::SystemNow()', new)
        new = re.sub(r'(?<![\w:])(?:std::)?time\s*\(\s*(nullptr|NULL|0)\s*\)', r'SandboxClock::UnixTime(\1)', new)
        if path.name.startswith('TaskScheduler'):
            new = new.replace('clock_t::now()', 'SandboxClock::SteadyNow()')
        if old != new:
            new = '#include "SandboxClock.h"\n' + new
            path.write_text(new, encoding='utf-8', newline='\n')
            changes[str(path.relative_to(source))] = {'before': hashlib.sha256(old.encode()).hexdigest(), 'after': digest(path)}
    main_cpp = source / 'src/server/apps/worldserver/Main.cpp'
    text = main_cpp.read_text(encoding='utf-8-sig')
    begin = text.index('void WorldUpdateLoop()\n{')
    end = text.index('\nvoid SignalHandler(', begin)
    text = '#include "SandboxClock.h"\n#include <cmath>\n' + text[:begin] + (HERE / 'world_loop.cpp.txt').read_text() + text[end:]
    main_cpp.write_text(text, encoding='utf-8', newline='\n')
    shutil.copy2(HERE / 'SandboxProbe.cpp', source / 'modules/mod-lazy-questing/src/CharacterControllerModule.cpp')
    # Different process name is mandatory: the normal launcher matches worldserver.exe globally.
    cmake = source / 'src/server/apps/CMakeLists.txt'
    with cmake.open('a') as file:
        file.write('\nset_target_properties(worldserver PROPERTIES OUTPUT_NAME "wowbot-lab-world")\n')
    artifacts = [main_cpp, cmake, utilities / 'SandboxClock.h',
                 source / 'modules/mod-lazy-questing/src/CharacterControllerModule.cpp']
    hashes = {str(p.relative_to(source)): digest(p) for p in artifacts}
    (root / 'source-manifest.json').write_text(json.dumps({'revisions': revisions, 'clock_overlay': changes,
        'overlay_artifacts': hashes, 'source_files': source_hashes(source)}, indent=2))
    print(f'Prepared {root}; clock overlay applied to {len(changes)} files; live baseline saved.')


if __name__ == '__main__':
    main()
