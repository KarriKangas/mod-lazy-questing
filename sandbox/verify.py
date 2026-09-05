"""Read-only checks of live-file integrity and sandbox identity."""
import argparse
import hashlib
import json
from pathlib import Path
from lab import check, mysql


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path('D:/wowbot-lab'))
    root = parser.parse_args().root.resolve()
    check(root)
    baseline = json.loads((root / 'live-baseline.json').read_text())
    failures = []
    for name, expected in baseline.items():
        with Path(name).open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != expected:
            failures.append(name)
    if failures:
        raise RuntimeError(f'Protected live files changed: {failures}')
    database = mysql(root, 'SELECT @@port,@@datadir;', admin=False)
    if '13316' not in database or str(root).replace('\\', '/').lower() not in database.replace('\\', '/').lower():
        raise RuntimeError('Unexpected database endpoint')
    population = mysql(root, 'SELECT COUNT(*) AS characters_total, SUM(online) AS online FROM characters; SELECT COUNT(*) AS registered, SUM(enabled AND always_online AND retired_at IS NULL) AS active FROM strict_altbots; SELECT COUNT(*) AS non_strict_characters FROM characters c LEFT JOIN strict_altbots s ON s.character_guid=c.guid WHERE s.character_guid IS NULL;', database='lab_characters', admin=False)
    print(f'PASS: {len(baseline)} protected live files unchanged; sandbox config and DB identity verified.')
    print(population)


if __name__ == '__main__':
    main()
