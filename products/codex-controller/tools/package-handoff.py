"""Create and verify a portable source/debug handoff, without user runtime data."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import subprocess
import sys
import zipfile

PRODUCT = Path(__file__).resolve().parents[1]
ROOT = PRODUCT.parents[1]
FIRMWARE_SHA = '1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9'
TOP = 'ai-passport-codex-controller'
ROOT_FILES = ['AGENTS.md', 'AGENTS.zh_CN.md', 'CLAUDE.md', 'CLAUDE.zh_CN.md',
              'LICENSE', '.gitignore', '.clangd', 'CMakeLists.txt',
              'dependencies.lock', 'partitions.csv', 'sdkconfig.defaults']
SOURCE_TREES = ['.github', 'assets', 'components', 'docs', 'main', 'skills',
                'tests', 'tools', 'products/codex-controller']
EXCLUDED_DIRS = {'.git', 'build', 'build-host', 'target', 'node_modules',
                 'managed_components', '.pnpm-store', '__pycache__', '.agents',
                 '.codex', '.vscode', '.idea', 'font-source'}
EXCLUDED_FILES = {'sdkconfig', 'sdkconfig.old', '.DS_Store', '.env', '.env.local'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def source_files():
    for name in ROOT_FILES:
        path = ROOT/name
        if not path.is_file():
            raise FileNotFoundError(path)
        yield path
    for name in SOURCE_TREES:
        base = ROOT/name
        pending = [base]
        while pending:
            directory = pending.pop()
            for path in sorted(directory.iterdir()):
                if path.is_symlink():
                    raise ValueError(f'Unexpected source symlink: {path.relative_to(ROOT)}')
                if path.is_dir():
                    if path.name not in EXCLUDED_DIRS:
                        pending.append(path)
                elif path.name not in EXCLUDED_FILES and path.suffix.lower() not in {
                    '.pyc', '.log', '.exe', '.dll', '.msi'
                }:
                    yield path


def mode_for(path, data):
    return 0o755 if path.suffix == '.sh' or data.startswith(b'#!') else 0o644


def info(name, date, mode, directory=False):
    stamp = (int(date[:4]), int(date[4:6]), int(date[6:8]), 0, 0, 0)
    entry = zipfile.ZipInfo(name, stamp)
    entry.create_system = 3
    entry.external_attr = ((stat.S_IFDIR if directory else stat.S_IFREG) | mode) << 16
    if directory:
        entry.external_attr |= 0x10
    entry.compress_type = zipfile.ZIP_DEFLATED
    return entry


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--date', required=True, help='Snapshot date YYYYMMDD')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if len(args.date) != 8 or not args.date.isdigit():
        parser.error('Use YYYYMMDD')
    output = args.output or ROOT/'build/handoff'/f'passport-codex-controller-mac-handoff-{args.date}.zip'
    output = output.resolve()
    if not output.is_relative_to((ROOT/'build').resolve()):
        parser.error('Handoff output must remain inside repository build/')
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        parser.error('Output already exists; select a new --output path to preserve it')

    firmware = PRODUCT/'build/firmware'/FIRMWARE_SHA
    subprocess.run([sys.executable, str(ROOT/'tools/archive_firmware.py'),
                    'verify', str(firmware)], check=True)
    files = {path.relative_to(ROOT).as_posix(): path for path in source_files()}
    fw_manifest = json.loads((firmware/'manifest.json').read_text(encoding='utf-8'))
    for name in ['manifest.json', *fw_manifest['files']]:
        path = firmware/name
        files[path.relative_to(ROOT).as_posix()] = path
    required = ['products/codex-controller/HANDOFF.md',
                'products/codex-controller/HANDOFF.zh_CN.md',
                'products/codex-controller/desktop/src/connection.rs',
                'products/codex-controller/desktop/Cargo.lock',
                'assets/fonts/NotoSansSC-Regular.otf', 'assets/fonts/passport_cjk_16.c',
                'components/bsp/include/bsp_pins.h']
    assert all(name in files for name in required)
    assert len({name.casefold() for name in files}) == len(files), 'Case-colliding paths'

    manifest = {
        'format': 1,
        'snapshot_date': f'{args.date[:4]}-{args.date[4:6]}-{args.date[6:8]}',
        'product_root': 'products/codex-controller',
        'source_has_git': (ROOT/'.git').exists(),
        'firmware_sha256': FIRMWARE_SHA,
        'files': {},
        'excluded': sorted(EXCLUDED_DIRS | EXCLUDED_FILES) + [
            'Windows binaries/toolchains', 'old firmware archives', 'raw runtime logs',
            'user configuration/conversation links', 'pairing secrets and recorded speech'],
        'manifest_self_hash': 'Excluded; verify whole archive with adjacent .sha256',
    }
    folders = {TOP+'/'}
    for name in files:
        parent = PurePosixPath(TOP)/PurePosixPath(name).parent
        while str(parent) != '.':
            folders.add(parent.as_posix()+'/')
            parent = parent.parent
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for folder in sorted(folders):
            z.writestr(info(folder, args.date, 0o755, True), b'')
        for name, path in sorted(files.items()):
            data = path.read_bytes()
            mode = mode_for(path, data)
            manifest['files'][name] = {'size': len(data), 'sha256': digest(data),
                                       'mode': f'{mode:04o}'}
            z.writestr(info(TOP+'/'+name, args.date, mode), data)
        data = (json.dumps(manifest, indent=2, ensure_ascii=True)+'\n').encode('utf-8')
        z.writestr(info(TOP+'/HANDOFF_MANIFEST.json', args.date, 0o644), data)

    with zipfile.ZipFile(output) as z:
        assert z.testzip() is None, 'ZIP CRC failure'
        assert len(z.namelist()) == len(set(z.namelist())), 'Duplicate ZIP entries'
        for name, entry in manifest['files'].items():
            archived = z.read(TOP+'/'+name)
            assert len(archived) == entry['size'] and digest(archived) == entry['sha256'], name
            assert stat.S_IMODE(z.getinfo(TOP+'/'+name).external_attr >> 16) == int(entry['mode'], 8), name
        for member in z.namelist():
            relative = PurePosixPath(member)
            assert not relative.is_absolute() and '..' not in relative.parts
            if not member.endswith('/'):
                assert '.git' not in relative.parts
                assert not (set(relative.parts) & (EXCLUDED_DIRS-{'build'})), member
                if 'build' in relative.parts:
                    assert member.startswith(TOP+'/products/codex-controller/build/firmware/'+FIRMWARE_SHA+'/')
    archive_hash = digest(output.read_bytes())
    output.with_suffix('.zip.sha256').write_text(archive_hash+'  '+output.name+'\n', encoding='ascii')
    print(json.dumps({'archive': str(output), 'bytes': output.stat().st_size,
                      'files': len(files), 'uncompressed_bytes': sum(e['size'] for e in manifest['files'].values()),
                      'sha256': archive_hash, 'crc_hashes_modes_paths': 'PASS'}, indent=2))


if __name__ == '__main__':
    main()
