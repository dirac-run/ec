import json
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = os.environ.get('EASYCOMMAND_TEST_DRIVER',str(ROOT / 'build/contract-driver'))

def response(value,kind='COMMAND'):
    return json.dumps({'kind':kind,'value':value})

def interactive(payload, answer, cwd):
    master, slave = pty.openpty()
    p = subprocess.Popen([DRIVER,payload],stdin=slave,stdout=slave,stderr=slave,cwd=cwd,close_fds=True)
    os.close(slave)
    output=b''
    deadline=time.monotonic()+5
    try:
        while b'Execute? [Y/n] ' not in output and p.poll() is None:
            if time.monotonic()>deadline:
                raise TimeoutError('confirmation prompt did not appear')
            if select.select([master],[],[],0.1)[0]:
                try:
                    output+=os.read(master,4096)
                except OSError:
                    break
        # The caller can inspect state at the actual confirmation boundary.
        if callable(answer):
            answer=answer(p)
        if answer is not None:
            os.write(master,answer)
        code=p.wait(timeout=5)
        while select.select([master],[],[],0)[0]:
            try:
                output+=os.read(master,4096)
            except OSError:
                break
        return code,output
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        os.close(master)

class Contract(unittest.TestCase):
    def test_exact_execution_and_default_yes(self):
        with tempfile.TemporaryDirectory() as d:
            target=Path(d)/'approved'
            cmd="printf '%s' 'a b' | tr 'a' 'z' > approved"
            for answer in (b'n\n',b'yes\n',b'y extra\n',b'\x04'):
                self.assertEqual(interactive(response(cmd),answer,d)[0],4)
                self.assertFalse(target.exists())
            def approve(_):
                self.assertFalse(target.exists())
                return b'y\n'
            code,output=interactive(response(cmd),approve,d)
            self.assertEqual(code,0)
            self.assertIn(cmd.encode(),output)
            self.assertNotIn(b'Command with TAB escaped:',output)
            self.assertEqual(target.read_text(),'z b')
            self.assertEqual(interactive(response('exit 23'),b'Y\n',d)[0],23)
            self.assertEqual(interactive(response('exit 24'),b'\n',d)[0],24)

    def test_cancellation(self):
        with tempfile.TemporaryDirectory() as d:
            def cancel(p):
                p.send_signal(signal.SIGINT)
                return None
            self.assertEqual(interactive(response('touch forbidden'),cancel,d)[0],-signal.SIGINT)
            self.assertFalse((Path(d)/'forbidden').exists())

    def test_noninteractive_preview_and_noncommands(self):
        with tempfile.TemporaryDirectory() as d:
            cmd=response('touch forbidden')
            p=subprocess.run([DRIVER,cmd],input='y\n',text=True,capture_output=True,cwd=d)
            self.assertEqual(p.returncode,4)
            p=subprocess.run([DRIVER,cmd,'preview'],text=True,capture_output=True,cwd=d)
            self.assertEqual(json.loads(p.stdout),json.loads(cmd))
            for kind,code in [('CLARIFY',2),('UNABLE',3)]:
                p=subprocess.run([DRIVER,response('touch forbidden',kind)],text=True,capture_output=True,cwd=d)
                self.assertEqual(p.returncode,code)
            self.assertFalse((Path(d)/'forbidden').exists())

    def test_control_syntax_and_startup_files(self):
        with tempfile.TemporaryDirectory() as d:
            for value in ['echo ok\n touch forbidden','echo ok\r touch forbidden',
                          'echo \x00hidden','echo \x1b[2J','echo \x7fhidden',
                          'echo \u2028hidden','echo \u202e hidden','if then',
                          '\t',' \t \t']:
                p=subprocess.run([DRIVER,response(value),'preview'],capture_output=True,cwd=d)
                self.assertEqual(p.returncode,1)
            for kind in ('CLARIFY','UNABLE'):
                p=subprocess.run([DRIVER,response('a\tb',kind),'preview'],capture_output=True,cwd=d)
                self.assertEqual(p.returncode,1)
            startup=Path(d)/'startup'
            startup.write_text('touch forbidden\n')
            env=dict(os.environ,BASH_ENV=str(startup))
            p=subprocess.run([DRIVER,response('true'),'preview'],env=env,capture_output=True,cwd=d)
            self.assertEqual(p.returncode,0)
            self.assertFalse((Path(d)/'forbidden').exists())

    def test_command_tab_is_exact_and_visible_at_confirmation(self):
        with tempfile.TemporaryDirectory() as d:
            command="printf '%s' 'a\tb' > 'out\\t.txt'"
            output_path=Path(d)/'out\\t.txt'
            preview=subprocess.run([DRIVER,response(command),'preview'],capture_output=True,cwd=d)
            self.assertEqual(preview.returncode,0,preview.stderr)
            self.assertEqual(json.loads(preview.stdout)['value'],command)
            self.assertIn(b'\\t',preview.stdout)
            code,output=interactive(response(command),b'n\n',d)
            self.assertEqual(code,4)
            self.assertIn(b'Command with TAB escaped:',output)
            self.assertIn(b'\\t',output)
            self.assertFalse(output_path.exists())
            code,output=interactive(response(command),b'y\n',d)
            self.assertEqual(code,0)
            self.assertEqual(output_path.read_bytes(),b'a\tb')

    def test_dependency_inspection_never_runs_commands(self):
        with tempfile.TemporaryDirectory() as d:
            for command in ['easycommand_missing_utility_xyz',
                            'printf x | easycommand_missing_utility_xyz',
                            'printf "%s" "$(easycommand_missing_utility_xyz)"',
                            'find . -exec easycommand_missing_utility_xyz {} +',
                            'command easycommand_missing_utility_xyz',
                            '"$(touch forbidden)"', 'eval "touch forbidden"']:
                p=subprocess.run([DRIVER,response(command),'preview'],capture_output=True,cwd=d)
                self.assertEqual(p.returncode,1,(command,p.stderr))
                self.assertFalse((Path(d)/'forbidden').exists())
            for command in ['[ -f file ]; printf "%s" "not-a-utility"',
                            "'printf' x", 'f() { printf x; }; f',
                            'printf "%s" "$(printf x)"',
                            'find . -type f -exec printf "%s\\n" {} +']:
                p=subprocess.run([DRIVER,response(command),'preview'],capture_output=True,cwd=d)
                self.assertEqual(p.returncode,0,(command,p.stderr))
            env=dict(os.environ,PATH=d)
            p=subprocess.run([DRIVER,response('sort file'),'preview'],env=env,capture_output=True,cwd=d)
            self.assertEqual(p.returncode,1)
            self.assertIn(b'required utility is unavailable: sort',p.stderr)

if __name__=='__main__':
    unittest.main()
