"""Package a native desktop build, notices and SHA-256 manifest (Python 3.11+)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tomllib
import zipfile

PRODUCT = Path(__file__).resolve().parents[1]
ROOT = PRODUCT.parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--platform', choices=['windows', 'macos'], required=True)
    args = parser.parse_args()
    destination = PRODUCT/'build'/f'desktop-{args.platform}'
    destination.mkdir(parents=True, exist_ok=True)
    release = PRODUCT/'desktop/target/release'
    if args.platform == 'windows':
        source = release/'passport-codex-bridge.exe'
        binary = destination/'passport-codex-bridge.exe'
        # Repack notices/docs without rewriting an identical running executable.
        # A different version still requires closing the application first.
        if not binary.exists() or hashlib.sha256(source.read_bytes()).digest() != hashlib.sha256(binary.read_bytes()).digest():
            shutil.copy2(source, binary)
    else:
        shutil.copytree(release/'Passport.app',
                        destination/'Passport.app', dirs_exist_ok=True)
    for name in ['README.md', 'README.zh_CN.md', 'PROTOCOL.md', 'PROTOCOL.zh_CN.md',
                 'VALIDATION.md', 'VALIDATION.zh_CN.md']:
        shutil.copy2(PRODUCT/name, destination/name)
    shutil.copy2(ROOT/'assets/fonts/NotoSansSC-OFL.txt', destination/'NotoSansSC-OFL.txt')
    shutil.copy2(ROOT/'LICENSE', destination/'LICENSE')
    cargo_home = Path(os.environ.get('CARGO_HOME', Path.home()/'.cargo'))
    packages = tomllib.loads((PRODUCT/'desktop/Cargo.lock').read_text(encoding='utf-8'))['package']
    notices = []
    for package in packages:
        if not package.get('source', '').startswith('registry+'):
            continue
        name = f"{package['name']}-{package['version']}"
        sources = list((cargo_home/'registry/src').glob(f'*/{name}'))
        if not sources:
            continue  # Inactive OS dependencies may not be downloaded in a native build.
        source = sources[0]
        info = tomllib.loads((source/'Cargo.toml').read_text(encoding='utf-8'))['package']
        notices.append({'crate': name, 'license': info.get('license'), 'license_file': info.get('license-file')})
        files = [p for p in source.iterdir() if p.is_file() and
                 p.name.upper().startswith(('LICENSE', 'LICENCE', 'COPYING', 'COPYRIGHT', 'NOTICE'))]
        if info.get('license-file'):
            p = (source/info['license-file']).resolve()
            if p.is_relative_to(source.resolve()) and p.is_file(): files.append(p)
        if (source/'opus/COPYING').exists(): files.append(source/'opus/COPYING')
        for file in files:
            target = destination/'third-party'/name/file.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
    (destination/'third-party.json').write_text(json.dumps(notices, indent=2)+'\n', encoding='utf-8')
    manifest = {file.relative_to(destination).as_posix(): hashlib.sha256(file.read_bytes()).hexdigest()
                for file in destination.rglob('*') if file.is_file() and file.name != 'manifest.json'}
    (destination/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    archive = destination.with_suffix('.zip')
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, strict_timestamps=False) as z:
        for file in destination.rglob('*'):
            if file.is_file(): z.write(file, file.relative_to(destination))
    print(f'Desktop package: {archive}; SHA256 {hashlib.sha256(archive.read_bytes()).hexdigest()}')


if __name__ == '__main__':
    main()
