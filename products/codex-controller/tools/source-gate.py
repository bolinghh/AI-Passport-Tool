"""Run the unchanged repository gates on a disposable source copy (source ZIPs)."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bash', default='bash')
    parser.add_argument('--mode', choices=['--static', '--firmware', '--all'], default='--all')
    args = parser.parse_args()
    build = ROOT/'build'
    build.mkdir(exist_ok=True)
    copy = Path(tempfile.mkdtemp(prefix='validation-source-', dir=build))
    shutil.copytree(ROOT, copy, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('.git', 'build', 'build-host', 'target', 'node_modules',
                                                 'managed_components', '.pnpm-store', '__pycache__', '.agents', '.build'))
    subprocess.run(['git', 'init', '--quiet', str(copy)], check=True)
    print(f'Disposable validation source: {copy}', flush=True)
    environment = os.environ.copy()
    if os.name == 'nt':
        # Git Bash orchestrates the unchanged gate; IDF itself runs in native Windows Python.
        shim = build/'native-gate'
        shim.mkdir(exist_ok=True)
        python = shlex.quote(Path(sys.executable).as_posix())
        (shim/'python3').write_text(f'#!/usr/bin/env bash\nexec {python} "$@"\n', encoding='utf-8')
        if environment.get('IDF_PATH'):
            native = shim/'native-idf.py'
            native.write_text('import os,runpy,sys\nfrom pathlib import Path\n'
                              "os.environ.pop('MSYSTEM',None)\n"
                              'sys.argv=sys.argv[1:]\nsys.path.insert(0,str(Path(sys.argv[0]).parent))\n'
                              "runpy.run_path(sys.argv[0],run_name='__main__')\n", encoding='utf-8')
            idf = Path(environment['IDF_PATH'])/'tools/idf.py'
            (shim/'idf.py').write_text(f'#!/usr/bin/env bash\nexec {python} '
                                       f'{shlex.quote(native.as_posix())} {shlex.quote(idf.as_posix())} "$@"\n', encoding='utf-8')
        elif args.mode != '--static':
            raise RuntimeError('Activate native ESP-IDF 5.5.3 before the firmware gate')
        environment['PATH'] = str(shim)+os.pathsep+environment.get('PATH', '')
    subprocess.run([args.bash, 'tools/validate.sh', args.mode], cwd=copy, check=True, env=environment)
    # Keep results for inspection; no recursive cleanup or changes to the original checkout.


if __name__ == '__main__':
    main()
