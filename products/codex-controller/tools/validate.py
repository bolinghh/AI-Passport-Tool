"""Native product validation; use an activated ESP-IDF 5.5.3 environment."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

PRODUCT = Path(__file__).resolve().parents[1]
ROOT = PRODUCT.parents[1]


def run(args, cwd=PRODUCT):
    print('+', ' '.join(map(str, args)), flush=True)
    environment = os.environ.copy()
    if sys.platform == 'darwin':
        # Keep the locked Opus dependency buildable with native CMake 4.
        environment.setdefault('CMAKE_POLICY_VERSION_MINIMUM', '3.5')
        environment['MACOSX_DEPLOYMENT_TARGET'] = '13.0'
    subprocess.run(list(map(str, args)), cwd=cwd, check=True, env=environment)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--host', action='store_true')
    p.add_argument('--firmware', action='store_true')
    args = p.parse_args()
    both = not args.host and not args.firmware
    if args.host or both:
        run(['cmake', '-S', PRODUCT/'tests', '-B', PRODUCT/'build-host/cmake'])
        run(['cmake', '--build', PRODUCT/'build-host/cmake', '--config', 'Debug'])
        run(['ctest', '--test-dir', PRODUCT/'build-host/cmake', '-C', 'Debug', '--output-on-failure'])
        preview = PRODUCT/'build-host/preview'
        run(['cmake', '-S', PRODUCT/'tests/preview', '-B', preview])
        run(['cmake', '--build', preview, '--parallel', '8'])
        images = PRODUCT/'build-host/ui-preview'
        images.mkdir(parents=True, exist_ok=True)
        run([preview/'screen_preview', images])
        run(['cargo', 'test', '--locked', '--manifest-path', PRODUCT/'desktop/Cargo.toml'])
        if sys.platform == 'darwin':
            run(['cargo', 'build', '--release', '--locked', '--manifest-path', PRODUCT/'desktop/Cargo.toml',
                 '--target-dir', PRODUCT/'desktop/target/macos13'])
            run(['swift', 'run', '--package-path', PRODUCT/'desktop/macos', 'PassportChecks'])
    if args.firmware or both:
        idf = Path(os.environ['IDF_PATH'])/'tools/idf.py'
        version = subprocess.check_output([sys.executable, str(idf), '--version'], text=True)
        if 'ESP-IDF v5.5.3' not in version:
            raise RuntimeError(f'Expected ESP-IDF v5.5.3, got {version}')
        build = PRODUCT/'build/firmware-build'
        build.mkdir(parents=True, exist_ok=True)
        run([sys.executable, idf, '-B', build, '-D', f'SDKCONFIG={build}/sdkconfig',
             '-D', f'SDKCONFIG_DEFAULTS={PRODUCT}/sdkconfig.defaults', '-D', 'IDF_TARGET=esp32c3', 'build'])
        run([sys.executable, idf, '-B', build, 'merge-bin', '-o', build/'FoloToy-AI-Passport-full.bin'])
        run([sys.executable, ROOT/'tools/verify_firmware.py', build])
        run([sys.executable, ROOT/'tools/archive_firmware.py', 'create', build,
             '--archive-root', PRODUCT/'build/firmware'])
        for package in (PRODUCT/'build/firmware').iterdir():
            if (package/'manifest.json').is_file():
                run([sys.executable, ROOT/'tools/archive_firmware.py', 'verify', package])


if __name__ == '__main__':
    main()
