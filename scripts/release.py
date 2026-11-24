#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
一键发版脚本：版本号更新 -> 编译 -> commit/tag/push -> GitHub Release 发布

用法:
    python scripts/release.py 5.9.7          # 编译 + 发版
    python scripts/release.py 5.9.7 --no-build  # 跳过编译（CI 或已有 bin 时）

发版完成后，打开设备页面 -> 更多设置 -> 固件更新 -> 一键升级 即可无线更新。
换电脑复用：clone 仓库 + 安装 PlatformIO（pip install platformio）即可，
GitHub 凭据在首次 push 时登录一次。可选的 GitHub Actions CI 见 docs/ci-release.yml.example
（启用需 git token 具有 workflow scope，步骤见该文件头注释）。
"""
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import urllib.request

REPO = 'S0DX/esp8266-powerstrip'

def run(cmd, **kw):
    print('$', ' '.join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        sys.exit(1)
    return r.stdout.strip()

def git_token():
    q = subprocess.run(['git', 'credential', 'fill'],
                       input='protocol=https\nhost=github.com\n\n',
                       capture_output=True, text=True)
    for line in q.stdout.splitlines():
        if line.startswith('password='):
            return line.split('=', 1)[1]
    return None

def gh_api(method, path, token, payload=None, raw=False, retries=3):
    url = 'https://api.github.com/' + path
    data = json.dumps(payload).encode() if payload is not None else None
    last_err = None
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, method=method, data=data)
            req.add_header('Authorization', 'token ' + token)
            req.add_header('Accept', 'application/vnd.github+json')
            if data:
                req.add_header('Content-Type', 'application/json')
            with urllib.request.urlopen(req, timeout=60) as resp:
                body = resp.read()
                return resp.status, (body if raw else json.loads(body or b'{}'))
        except Exception as e:
            last_err = e
            print(f'  API 网络错误（第 {attempt+1} 次）: {e}，重试…')
    raise last_err

def main():
    args = [a for a in sys.argv[1:]]
    build = '--no-build' not in args
    ver = next((a for a in args if not a.startswith('--')), None)
    if not ver:
        print('用法: python scripts/release.py <版本号> [--no-build]')
        sys.exit(1)
    ver = ver.lstrip('v')
    if not re.match(r'^\d+\.\d+\.\d+$', ver):
        print('版本号格式应为 x.y.z')
        sys.exit(1)
    tag = 'v' + ver
    root = pathlib.Path(__file__).resolve().parent.parent

    # 1. 版本号
    cfg = root / 'src' / 'config.h'
    s = cfg.read_text(encoding='utf-8')
    new = re.sub(r'#define VERSION "[^"]+"', f'#define VERSION "{ver}"', s, count=1)
    if new != s:
        cfg.write_text(new, encoding='utf-8')
        print('config.h VERSION ->', ver)
    else:
        print('config.h 已是', ver)

    # 2. 编译
    dist = root / 'dist'
    dist.mkdir(exist_ok=True)
    bin_name = f'firmware-{ver}.bin'
    bin_path = dist / bin_name
    if build:
        run(['pio', 'run', '-e', 'esp12e'])
    src_bin = root / '.pio' / 'build' / 'esp12e' / 'firmware.bin'
    if not src_bin.exists():
        print('未找到编译产物 .pio/build/esp12e/firmware.bin（--no-build 模式需自行准备 dist bin）')
        sys.exit(1)
    bin_path.write_bytes(src_bin.read_bytes())
    sha = hashlib.sha256(bin_path.read_bytes()).hexdigest()
    sha_path = dist / (bin_name + '.sha256')
    sha_path.write_text(f'{sha}  {bin_name}\n', encoding='utf-8')
    print(f'dist/{bin_name}: {bin_path.stat().st_size} bytes | sha256: {sha}')

    # 3. git
    for f in ['src/config.h', f'dist/{bin_name}', f'dist/{bin_name}.sha256']:
        run(['git', 'add', str(root / f)])
    run(['git', 'commit', '-m', 'release: v' + ver])
    run(['git', 'tag', tag])
    pushed = False
    for attempt in range(4):
        try:
            run(['git', '-c', 'http.version=HTTP/1.1', 'push', 'origin', 'main', tag])
            pushed = True
            break
        except SystemExit:
            print('push 网络抖动，重试…')
    if not pushed:
        sys.exit(1)

    # 4. GitHub Release
    token = git_token()
    if not token:
        print('未获取到 GitHub 凭据（git credential）——Release 未创建，可手动在网页发布')
        sys.exit(0)
    body = (
        f'## v{ver}\n\n'
        f'- 自动发版（scripts/release.py）\n\n'
        f'## 校验\n| 文件 | SHA256 |\n|------|--------|\n'
        f'| {bin_name} | `{sha}` |'
    )
    status, rel = gh_api('POST', f'repos/{REPO}/releases', token,
                         {'tag_name': tag, 'name': 'v' + ver, 'body': body})
    rel_id = rel.get('id')
    if not rel_id:
        status, rel = gh_api('GET', f'repos/{REPO}/releases/tags/{tag}', token)
        rel_id = rel['id']
        print('Release 已存在（id', rel_id, '），直接补传附件')
    # 5. 上传 asset
    for name, fpath in [(bin_name, bin_path), (bin_name + '.sha256', sha_path)]:
        up = urllib.request.Request(
            f'https://uploads.github.com/repos/{REPO}/releases/{rel_id}/assets?name={name}',
            method='POST', data=fpath.read_bytes())
        up.add_header('Authorization', 'token ' + token)
        up.add_header('Content-Type', 'application/octet-stream')
        with urllib.request.urlopen(up, timeout=300) as resp:
            print('asset:', name, '->', json.loads(resp.read()).get('state'))
    print()
    print(f'v{ver} 发布完成: https://github.com/{REPO}/releases/tag/{tag}')
    print('现在打开设备页面 -> 更多设置 -> 固件更新 -> 一键升级 即可无线更新。')

if __name__ == '__main__':
    main()
