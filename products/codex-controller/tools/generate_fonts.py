"""Generate a bounded, licensed Chinese font and the shared desktop coverage list."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import urllib.request

PRODUCT = Path(__file__).resolve().parents[1]
ROOT = PRODUCT.parents[1]
ASSETS = ROOT / "assets" / "fonts"
FONT_URL = "https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf"
LICENSE_URL = "https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/LICENSE"


def inventory():
    points = set(range(0x20, 0x7f))
    for first in range(0xb0, 0xf8):
        for second in range(0xa1, 0xff):
            try:
                c = bytes([first, second]).decode("gb2312")
                if 0x4e00 <= ord(c) <= 0x9fff:
                    points.add(ord(c))
            except UnicodeDecodeError:
                pass
    points.update(map(ord, "，。！？：；（）【】《》、—…·"))
    for c in (PRODUCT / "main/screen.c").read_text(encoding="utf-8"):
        if ord(c) >= 0x80:
            points.add(ord(c))
    return sorted(points)


def download(url, path):
    if not path.exists():
        with urllib.request.urlopen(url, timeout=120) as response:
            path.write_bytes(response.read())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--node", default="node")
    parser.add_argument("--inventory-only", action="store_true")
    args = parser.parse_args()
    points = inventory()
    (PRODUCT / "font-codepoints.json").write_text(json.dumps(points) + "\n", encoding="utf-8")
    if args.inventory_only:
        print(f"Coverage inventory: {len(points)} code points")
        return
    ASSETS.mkdir(parents=True, exist_ok=True)
    font = ASSETS / "NotoSansSC-Regular.otf"
    license_file = ASSETS / "NotoSansSC-OFL.txt"
    download(FONT_URL, font)
    download(LICENSE_URL, license_file)
    # Verify the source really contains every advertised glyph before conversion.
    from fontTools.ttLib import TTFont
    source = TTFont(font)
    missing = set(points) - set(source.getBestCmap())
    if missing:
        raise RuntimeError(f"Source font missing glyphs: {sorted(missing)}")
    source.close()
    converter = PRODUCT / "tools/node_modules/lv_font_conv/lv_font_conv.js"
    subprocess.run([args.node, str(converter), "--font", font.relative_to(ROOT).as_posix(),
                    "--symbols", "".join(map(chr, points)), "--size", "16", "--bpp", "2",
                    "--format", "lvgl", "--no-compress", "--lv-font-name", "passport_cjk_16",
                    "--lv-include", "lvgl.h", "--output", (ASSETS / "passport_cjk_16.c").relative_to(ROOT).as_posix()], check=True, cwd=ROOT)
    manifest = {"source": FONT_URL, "license": "SIL OFL 1.1", "source_sha256": hashlib.sha256(font.read_bytes()).hexdigest(),
                "license_sha256": hashlib.sha256(license_file.read_bytes()).hexdigest(),
                "generated_sha256": hashlib.sha256((ASSETS / "passport_cjk_16.c").read_bytes()).hexdigest(),
                "converter": "lv_font_conv 1.5.3", "size": 16, "bpp": 2, "codepoints": len(points)}
    (PRODUCT / "font-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(points)} glyphs; source hash {manifest['source_sha256']}")


if __name__ == "__main__":
    main()
