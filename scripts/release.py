#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
本地发版辅助脚本（可选）：
    python scripts/release.py 5.9.7

作用：更新 src/config.h 的 VERSION -> git commit -> 打 tag -> push。
构建与 Release 发布由 GitHub Actions 自动完成（.github/workflows/release.yml），
push tag 后约 3-5 分钟 Release 即上线，随后手机端「一键升级」无线更新。

换电脑复用：只需 clone 本仓库（push 时登录 GitHub 账号），无需安装
PlatformIO 或任何构建环境——构建在 CI 进行。
"""
import re
import subprocess
import sys
import pathlib

def run(cmd, **kw):
    print('$', ' '.join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        sys.exit(1)
    return r.stdout.strip()

def main():
    if len(sys.argv) < 2:
        print('用法: python scripts/release.py <版本号，如 5.9.7>')
        sys.exit(1)
    ver = sys.argv[1].lstrip('v')
    if not re.match(r'^\d+\.\d+\.\d+$', ver):
        print('版本号格式应为 x.y.z')
        sys.exit(1)
    tag = 'v' + ver

    root = pathlib.Path(__file__).resolve().parent.parent
    cfg = root / 'src' / 'config.h'
    s = cfg.read_text(encoding='utf-8')
    new = re.sub(r'#define VERSION "[^"]+"', f'#define VERSION "{ver}"', s, count=1)
    if new == s:
        print('警告: config.h 版本未变化（可能已是 ' + ver + '）')
    else:
        cfg.write_text(new, encoding='utf-8')
        print('config.h VERSION ->', ver)

    run(['git', 'add', str(cfg)])
    run(['git', 'commit', '-m', 'release: v' + ver])
    run(['git', 'tag', tag])
    run(['git', 'push', 'origin', 'main', tag])
    print()
    print('完成: tag ' + tag + ' 已推送，GitHub Actions 正在自动构建发布。')
    print('约 3-5 分钟后在 https://github.com/S0DX/esp8266-powerstrip/releases 出现 Release，')
    print('之后打开设备页面 -> 更多设置 -> 固件更新 -> 一键升级 即可无线更新。')

if __name__ == '__main__':
    main()
