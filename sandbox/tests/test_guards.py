import sys
import os
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lab import check, is_live
from setup_runtime import write_config


class IsolationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        (self.root / '.wowbot-lab').touch()
        (self.root / 'runtime-ready.json').write_text('{}')
        self.world = {'LoginDatabaseInfo': '127.0.0.1;13316;botlab;test;lab_auth',
                      'CharacterDatabaseInfo': '127.0.0.1;13316;botlab;test;lab_characters',
                      'WorldDatabaseInfo': '127.0.0.1;13316;botlab;test;lab_world',
                      'BindIP': '127.0.0.1', 'WorldServerPort': '18085',
                      'DataDir': str(self.root / 'data'), 'LogsDir': str(self.root / 'logs'), 'PidFile': str(self.root / 'world.pid')}
        self.bots = {'PlayerbotsDatabaseInfo': '127.0.0.1;13316;botlab;test;lab_playerbots', 'AiPlayerbot.BotCheats': ''}
        for key in ('RandomBotAutologin', 'BotAutologin', 'MinRandomBots', 'MaxRandomBots', 'RandomBotAccountCount', 'AddClassAccountPoolSize', 'AutoTeleportForLevel'):
            self.bots['AiPlayerbot.' + key] = '0'
        self.save()

    def save(self):
        write_config(self.root / 'runtime/configs/worldserver.conf', self.world)
        write_config(self.root / 'runtime/configs/modules/playerbots.conf', self.bots)

    def tearDown(self):
        self.temp.cleanup()

    def test_valid(self):
        self.assertEqual(check(self.root)['BindIP'], '127.0.0.1')

    def test_live_database_rejected(self):
        self.world['CharacterDatabaseInfo'] = '127.0.0.1;3306;acore;test;acore_characters'
        self.save()
        with self.assertRaisesRegex(RuntimeError, 'Unsafe database'):
            check(self.root)

    def test_live_path_rejected(self):
        self.world['DataDir'] = 'D:/wowserver/data'
        self.save()
        with self.assertRaisesRegex(RuntimeError, 'escapes sandbox'):
            check(self.root)

    def test_network_exposure_rejected(self):
        self.world['BindIP'] = '0.0.0.0'
        self.save()
        with self.assertRaisesRegex(RuntimeError, 'listener'):
            check(self.root)

    def test_extra_pool_rejected(self):
        self.bots['AiPlayerbot.AddClassAccountPoolSize'] = '50'
        self.save()
        with self.assertRaisesRegex(RuntimeError, 'population'):
            check(self.root)

    def test_cheats_rejected(self):
        self.bots['AiPlayerbot.BotCheats'] = 'taxi'
        self.save()
        with self.assertRaisesRegex(RuntimeError, 'cheats'):
            check(self.root)

    def test_process_identity(self):
        self.assertTrue(is_live(os.getpid(), sys.executable))
        self.assertFalse(is_live(os.getpid(), self.root / 'wowbot-lab-world.exe'))


if __name__ == '__main__':
    unittest.main()
