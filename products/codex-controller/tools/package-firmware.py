"""Package a verified firmware archive with matching debug files and notices."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import zipfile

PRODUCT = Path(__file__).resolve().parents[1]
ROOT = PRODUCT.parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('archive', type=Path, help='Verified build/firmware/<sha256> directory')
    parser.add_argument('--idf-path', type=Path, default=os.environ.get('IDF_PATH'),
                        help='ESP-IDF 5.5.3 source directory for SDK notices')
    args = parser.parse_args()
    if args.idf_path is None or not (args.idf_path/'LICENSE').is_file():
        parser.error('Activate ESP-IDF 5.5.3 or supply --idf-path for SDK notices')
    archive = args.archive.resolve(strict=True)
    subprocess.run([sys.executable, str(ROOT/'tools/archive_firmware.py'), 'verify', str(archive)], check=True)
    destination = PRODUCT/'build/codex-controller-firmware.zip'
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED, strict_timestamps=False) as output:
        for file in sorted(archive.rglob('*')):
            if file.is_file():
                output.write(file, 'firmware/'+file.relative_to(archive).as_posix())
        for name in ['README.md', 'README.zh_CN.md', 'PROTOCOL.md', 'PROTOCOL.zh_CN.md',
                     'VALIDATION.md', 'VALIDATION.zh_CN.md']:
            output.write(PRODUCT/name, name)
        output.write(ROOT/'LICENSE', 'LICENSE')
        output.write(ROOT/'assets/fonts/NotoSansSC-OFL.txt', 'third-party/NotoSansSC-OFL.txt')
        for component in sorted((PRODUCT/'managed_components').iterdir()):
            if not component.is_dir():
                continue
            for file in component.iterdir():
                if file.is_file() and file.name.upper().startswith(('LICENSE', 'LICENCE', 'COPYING', 'NOTICE')):
                    output.write(file, f'third-party/{component.name}/{file.name}')
        output.write(PRODUCT/'dependencies.lock', 'dependencies.lock')
        output.write(args.idf_path/'LICENSE', 'third-party/esp-idf/LICENSE')
        # Retain the SDK's bundled third-party notices, including NimBLE and RTOS.
        for file in sorted((args.idf_path/'components').rglob('*')):
            if file.is_file() and file.name.upper().startswith(('LICENSE', 'LICENCE', 'COPYING', 'NOTICE')):
                output.write(file, 'third-party/esp-idf/'+file.relative_to(args.idf_path).as_posix())
    print(f'Firmware package: {destination}; SHA256 {hashlib.sha256(destination.read_bytes()).hexdigest()}')


if __name__ == '__main__':
    main()
