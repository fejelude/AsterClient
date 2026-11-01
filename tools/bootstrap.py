"""Fetch pinned, permissively licensed build dependencies without installing software."""
from pathlib import Path
import argparse
import hashlib
import io
import json
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PINNED = {entry['path']: entry for entry in json.loads((ROOT/'dependencies.lock.json').read_text())['files']}
IMGUI = 'f5befd2d29e66809cd1110a152e375a7f1981f06'
JSON = '55f93686c01528224f448c19128836e7df245f72'
MINHOOK = 'c3fcafdc10146beb5919319d0683e44e3c30d537'
ENTT = '4a2d1a8541228a90e02a873dba6a980506c42c03'
FILES = [
    'imgui.cpp', 'imgui.h', 'imgui_draw.cpp', 'imgui_tables.cpp',
    'imgui_widgets.cpp', 'imgui_demo.cpp', 'imgui_internal.h', 'imconfig.h',
    'imstb_rectpack.h', 'imstb_textedit.h', 'imstb_truetype.h', 'LICENSE.txt',
    'backends/imgui_impl_win32.cpp', 'backends/imgui_impl_win32.h',
    'backends/imgui_impl_dx11.cpp', 'backends/imgui_impl_dx11.h',
    'examples/example_win32_directx11/main.cpp',
]

def verify(data, target, url=None):
    path = str(target.relative_to(ROOT)).replace('\\', '/')
    pinned = PINNED.get(path)
    if not pinned or (url is not None and pinned['url'] != url):
        raise ValueError('Dependency is not pinned: '+path)
    digest = hashlib.sha256(data).hexdigest()
    if digest != pinned['sha256']:
        raise ValueError('Dependency hash changed: '+path)
    return {'path': path, 'url': pinned['url'], 'sha256': digest}

def fetch(url, target):
    if target.exists():
        data = target.read_bytes()
        return verify(data, target, url)
    else:
        with urllib.request.urlopen(url, timeout=90) as response:
            data = response.read()
        record = verify(data, target, url)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return record

def main():
    records = []
    for filename in FILES:
        records.append(fetch(f'https://raw.githubusercontent.com/ocornut/imgui/{IMGUI}/{filename}',
                             ROOT/'external'/'imgui'/filename))
    for source, target in [('single_include/nlohmann/json.hpp', 'json.hpp'),
                           ('LICENSE.MIT', 'LICENSE.MIT')]:
        records.append(fetch(f'https://raw.githubusercontent.com/nlohmann/json/{JSON}/{source}',
                             ROOT/'external'/'json'/target))
    for filename in ['CMakeLists.txt','LICENSE.txt','cmake/minhook-config.cmake.in','include/MinHook.h','src/buffer.c','src/buffer.h',
                     'src/hook.c','src/trampoline.c','src/trampoline.h','src/hde/hde64.c',
                     'src/hde/hde64.h','src/hde/hde32.c','src/hde/hde32.h','src/hde/pstdint.h',
                     'src/hde/table64.h','src/hde/table32.h']:
        records.append(fetch(f'https://raw.githubusercontent.com/TsudaKageyu/minhook/{MINHOOK}/{filename}',ROOT/'external'/'minhook'/filename))
    for source,target in [('single_include/entt/entt.hpp','entt.hpp'),('LICENSE','LICENSE')]:
        records.append(fetch(f'https://raw.githubusercontent.com/skypjack/entt/{ENTT}/{source}',ROOT/'external'/'entt'/target))
    sdk_commit='c0e9cf1ce5a6dde1d3cc6665e36a8ecd796e71cd'
    sdk_archive=ROOT/'tools'/'vendor'/'bedrock-sdk-reference.zip'
    records.append(fetch(f'https://codeload.github.com/b01dev/MCBE_DX12_ModBase/zip/{sdk_commit}',sdk_archive))
    with zipfile.ZipFile(sdk_archive) as sdk:
        marker=f'MCBE_DX12_ModBase-{sdk_commit}/external/entt/'
        for entry in sdk.infolist():
            if not entry.filename.startswith(marker) or entry.is_dir(): continue
            relative=entry.filename[len(marker):]
            target=ROOT/'external'/'bedrock-sdk'/'entt'/relative
            if not target.resolve().is_relative_to((ROOT/'external'/'bedrock-sdk').resolve()):raise ValueError('Unsafe SDK archive path')
            data=sdk.read(entry)
            # Verify every member before writing it, including an existing cached copy.
            record=verify(target.read_bytes() if target.exists() else data,target)
            target.parent.mkdir(parents=True,exist_ok=True)
            if not target.exists():target.write_bytes(data)
            records.append(record)
    records.append(fetch(f'https://raw.githubusercontent.com/b01dev/MCBE_DX12_ModBase/{sdk_commit}/LICENSE',ROOT/'external'/'bedrock-sdk'/'LICENSE'))
    cmake = ROOT/'tools'/'vendor'/'cmake-3.31.8-windows-x86_64'/'bin'/'cmake.exe'
    if not cmake.exists():
        archive = ROOT/'tools'/'vendor'/'cmake-3.31.8-windows-x86_64.zip'
        records.append(fetch('https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-windows-x86_64.zip', archive))
        with zipfile.ZipFile(io.BytesIO(archive.read_bytes())) as package:
            destination = archive.parent.resolve()
            for entry in package.infolist():
                target = (destination/entry.filename).resolve()
                if not target.is_relative_to(destination):
                    raise ValueError('Unsafe archive path')
            package.extractall(destination)
    # Bootstrap consumes the reviewed lock; it never blesses downloaded bytes by rewriting it.
    print(f'Pinned dependencies ready: {len(records)} files')
    print(cmake)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only', action='store_true', help='Verify vendored source files without downloading build tools')
    args = parser.parse_args()
    if args.verify_only:
        count = 0
        for path in PINNED:
            target = ROOT/path
            if path.startswith('external/') or target.exists():
                verify(target.read_bytes(), target)
                count += 1
        print(f'Verified {count} pinned files; no downloads or changes')
    else:
        main()
