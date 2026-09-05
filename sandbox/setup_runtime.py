"""Private database instance + runtime. Only reads the existing server/databases."""
import argparse
import json
import re
import secrets
import shutil
import socket
import subprocess
import time
from pathlib import Path

MYSQL_BIN = Path('C:/Program Files/MySQL/MySQL Server 8.4/bin')
LIVE_BIN = Path('D:/wowserver/build/bin/RelWithDebInfo')
PORT = 13316


def read_config(path):
    result = {}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        if match := re.match(r'^\s*([\w.]+)\s*=\s*(.*?)\s*$', line):
            result[match[1]] = match[2].strip('"')
    return result


def write_config(path, values):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(f'{k} = "{v}"' for k, v in values.items()) + '\n', encoding='utf-8')


def clear_copied_bot_identity(root):
    # Keep world/item/navigation caches, discard old account, character and experiment state.
    tables = mysql(root, 'SHOW TABLES;', database='lab_playerbots').strip().splitlines()[1:]
    explicit = {'playerbots_custom_strategy', 'playerbots_db_store', 'playerbots_guild_tasks',
                'playerbots_preferred_mounts', 'playerbots_random_bots'}
    for table in tables:
        if table in explicit or table.startswith(('playerbots_account_', 'playerbots_economic_')):
            if not re.fullmatch(r'[a-z_]+', table):
                raise RuntimeError('Unexpected table name')
            mysql(root, f'TRUNCATE TABLE `{table}`;', database='lab_playerbots')


def client_file(path, user, password, port=PORT, host='127.0.0.1'):
    def quote(value):
        return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"') + '"'
    path.write_text(f'[client]\nhost={quote(host)}\nport={port}\nuser={quote(user)}\npassword={quote(password)}\nprotocol=TCP\n', encoding='utf-8')


def mysql(root, sql, admin=True, database=None):
    options = root / ('mysql-admin.cnf' if admin else 'mysql-client.cnf')
    command = [str(MYSQL_BIN / 'mysql.exe'), f'--defaults-file={options}', '--host=127.0.0.1',
               f'--port={PORT}', '--protocol=TCP', '--batch', '--raw']
    if database:
        command.append(database)
    result = subprocess.run(command, input=sql, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f'MySQL command failed: {result.stderr}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path('D:/wowbot-lab'))
    root = parser.parse_args().root.resolve()
    if not (root / '.wowbot-lab').exists() or root == Path('D:/wowserver') or Path('D:/wowserver') in root.parents:
        raise RuntimeError('Not an isolated marked sandbox')
    if (root / 'runtime-ready.json').exists():
        raise RuntimeError('Runtime already prepared')
    runtime = root / 'runtime'
    runtime.mkdir(exist_ok=True)
    (root / 'logs').mkdir(exist_ok=True)
    # ACL private runtime credentials and copied DB contents to the current user and SYSTEM.
    user = subprocess.check_output(['whoami'], text=True).strip()
    subprocess.run(['icacls', str(root), '/inheritance:r', '/grant:r', f'{user}:(OI)(CI)F', 'SYSTEM:(OI)(CI)F'], check=True, capture_output=True)
    dbdir = root / 'mysql-data'
    if not dbdir.exists():
        with socket.socket() as sock:
            if sock.connect_ex(('127.0.0.1', PORT)) == 0:
                raise RuntimeError('Sandbox database port already occupied')
        dbdir.mkdir()
        subprocess.run([str(MYSQL_BIN / 'mysqld.exe'), '--no-defaults', '--initialize-insecure', f'--basedir={MYSQL_BIN.parent}', f'--datadir={dbdir}'], check=True)
    ini = root / 'mysql.ini'
    ini.write_text(f'[mysqld]\nbasedir={MYSQL_BIN.parent.as_posix()}\ndatadir={dbdir.as_posix()}\nport={PORT}\nbind-address=127.0.0.1\nmysqlx=OFF\nskip-log-bin\ninnodb-buffer-pool-size=268435456\nperformance-schema=OFF\nlog-error={(root / "logs/mysql.log").as_posix()}\n', encoding='utf-8')
    process = subprocess.Popen([str(MYSQL_BIN / 'mysqld.exe'), f'--defaults-file={ini}'], creationflags=subprocess.CREATE_NO_WINDOW)
    (root / 'mysql.pid').write_text(str(process.pid))
    if not (root / 'mysql-admin.cnf').exists():
        client_file(root / 'mysql-admin.cnf', 'root', '')
    for attempt in range(60):
        try:
            mysql(root, 'SELECT 1;')
            break
        except RuntimeError:
            if process.poll() is not None:
                raise RuntimeError('Sandbox MySQL stopped; inspect logs/mysql.log')
            time.sleep(1)
    else:
        raise RuntimeError('Sandbox MySQL did not become ready')
    if not (root / 'db-secrets.json').exists():
        credentials = {'root': secrets.token_hex(24), 'botlab': secrets.token_hex(24)}
        mysql(root, f"ALTER USER 'root'@'localhost' IDENTIFIED BY '{credentials['root']}';")
        client_file(root / 'mysql-admin.cnf', 'root', credentials['root'])
        (root / 'db-secrets.json').write_text(json.dumps(credentials))
    credentials = json.loads((root / 'db-secrets.json').read_text())
    client_file(root / 'mysql-client.cnf', 'botlab', credentials['botlab'])
    world = read_config(LIVE_BIN / 'configs/worldserver.conf')
    bots = read_config(LIVE_BIN / 'configs/modules/playerbots.conf')
    sources = {**world, **bots}
    dumps = root / 'database-snapshots'
    dumps.mkdir(exist_ok=True)
    databases = {'LoginDatabaseInfo': ('lab_auth', True), 'CharacterDatabaseInfo': ('lab_characters', True),
                 'WorldDatabaseInfo': ('lab_world', False), 'PlayerbotsDatabaseInfo': ('lab_playerbots', False)}
    for key, (destination, schema_only) in databases.items():
        host, port, user, password, database = sources[key].split(';')[:5]
        if host not in ('127.0.0.1', 'localhost'):
            raise RuntimeError('Source DB must be local')
        source_options = root / 'source-client.cnf'
        client_file(source_options, user, password, int(port), host)
        dump = dumps / f'{destination}.sql'
        if not dump.exists():
            temp = dump.with_suffix('.partial')
            command = [str(MYSQL_BIN / 'mysqldump.exe'), f'--defaults-file={source_options}', '--single-transaction', '--skip-lock-tables', '--skip-triggers', '--no-tablespaces', '--set-gtid-purged=OFF', '--column-statistics=0', '--hex-blob']
            if schema_only:
                command.append('--no-data')
            with temp.open('wb') as output:
                result = subprocess.run(command + [database], stdout=output, stderr=subprocess.PIPE)
            if result.returncode:
                raise RuntimeError(f'Dump failed for {destination}: {result.stderr.decode()}')
            temp.rename(dump)
        source_options.unlink(missing_ok=True)
        mysql(root, f'CREATE DATABASE IF NOT EXISTS {destination} CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;')
        marker = dumps / f'{destination}.imported'
        if not marker.exists():
            with dump.open('rb') as data:
                result = subprocess.run([str(MYSQL_BIN / 'mysql.exe'), f'--defaults-file={root / "mysql-admin.cnf"}', '--host=127.0.0.1', f'--port={PORT}', '--protocol=TCP', destination], stdin=data, capture_output=True)
            if result.returncode:
                raise RuntimeError(f'Import failed: {result.stderr.decode()}')
            marker.touch()
        print(f'Copied {destination}: {"schema only (fresh population)" if schema_only else "content and caches"}', flush=True)
    mysql(root, f"CREATE USER IF NOT EXISTS 'botlab'@'127.0.0.1' IDENTIFIED BY '{credentials['botlab']}';")
    for database, _ in databases.values():
        mysql(root, f"GRANT ALL ON {database}.* TO 'botlab'@'127.0.0.1';")
    clear_copied_bot_identity(root)
    # Required fresh-install seed from core's active_arena_season.sql; no character state.
    mysql(root, 'INSERT INTO active_arena_season (season_id,season_state) SELECT 8,1 WHERE NOT EXISTS (SELECT 1 FROM active_arena_season);', database='lab_characters')
    mysql(root, "INSERT INTO realmlist (id,name,address,localAddress,localSubnetMask,port,icon,flag,timezone,allowedSecurityLevel,population,gamebuild) VALUES (1,'Bot Laboratory','127.0.0.1','127.0.0.1','255.255.255.0',18085,0,0,1,0,0,12340);", database='lab_auth')
    world.update({'RealmID': '1', 'WorldServerPort': '18085', 'BindIP': '127.0.0.1', 'DataDir': (root / 'data').as_posix(),
                  'LogsDir': (root / 'logs').as_posix(), 'PidFile': (root / 'world.pid').as_posix(), 'Console.Enable': '1',
                  'Ra.Enable': '0', 'SOAP.Enabled': '0', 'SOAP.IP': '127.0.0.1', 'SOAP.Port': '17878',
                  'Updates.EnableDatabases': '0', 'Updates.AutoSetup': '0', 'MaxCoreStuckTime': '0', 'Sandbox.Speed': '4', 'Sandbox.StepMs': '50',
                  'Rate.XP.Kill': '1', 'Rate.XP.Quest': '1', 'Rate.XP.Explore': '1', 'Rate.Drop.Money': '1'})
    for key, (database, _) in databases.items():
        (bots if key == 'PlayerbotsDatabaseInfo' else world)[key] = f'127.0.0.1;{PORT};botlab;{credentials["botlab"]};{database}'
    bots.update({'AiPlayerbot.Enabled': '1', 'AiPlayerbot.RandomBotAutologin': '0', 'AiPlayerbot.BotAutologin': '0',
                 'AiPlayerbot.MinRandomBots': '0', 'AiPlayerbot.MaxRandomBots': '0', 'AiPlayerbot.RandomBotAccountCount': '0',
                 'AiPlayerbot.RandomBotGuildCount': '0', 'AiPlayerbot.RandomBotArenaTeam2v2Count': '0',
                 'AiPlayerbot.RandomBotArenaTeam3v3Count': '0', 'AiPlayerbot.RandomBotArenaTeam5v5Count': '0',
                 'AiPlayerbot.RandomBotAutoJoinBG': '0', 'AiPlayerbot.RandomBotJoinLfg': '0', 'AiPlayerbot.RandomBotJoinBG': '0',
                 'AiPlayerbot.AddClassAccountPoolSize': '0', 'AiPlayerbot.DisabledWithoutRealPlayer': '0',
                 'AiPlayerbot.BotCheats': '', 'AiPlayerbot.AutoTeleportForLevel': '0', 'AiPlayerbot.RandomBotXPRate': '1'})
    write_config(runtime / 'configs/worldserver.conf', world)
    write_config(runtime / 'configs/modules/playerbots.conf', bots)
    write_config(runtime / 'configs/modules/StrictAltbotGuild.conf', {'StrictAltbotGuild.Enable': '1'})
    for dll in LIVE_BIN.glob('*.dll'):
        shutil.copy2(dll, runtime / dll.name)
    # A real copy: no junctions, hardlinks, or writable sharing with the play installation.
    shutil.copytree(Path('D:/wowserver/data'), root / 'data', dirs_exist_ok=True)
    (root / 'runtime-ready.json').write_text(json.dumps({'mysql_port': PORT, 'world_port': 18085, 'databases': [v[0] for v in databases.values()], 'population': 'fresh; no copied accounts or characters'}, indent=2))
    print('Private runtime ready. No authserver is needed for headless strict bots.', flush=True)


if __name__ == '__main__':
    main()
