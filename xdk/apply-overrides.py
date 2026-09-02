#!/usr/bin/env python3
"""Apply the source overrides retained in dependency-overrides to deps-src.
Safe to run repeatedly; files are copied byte-for-byte over the pinned checkout.
"""
from pathlib import Path
import argparse, shutil

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--src', required=True, help='xdk/deps-src directory')
    a=ap.parse_args()
    deps=Path(a.src).resolve()
    root=Path(__file__).resolve().parent.parent
    overrides=root/'dependency-overrides'
    if not overrides.is_dir():
        raise SystemExit('dependency-overrides directory is missing')
    copied=0
    # Override paths are relative to dependency roots named by their top folder.
    roots={
        'opus': deps/'opus',
        'jansson': deps/'jansson',
        'opennow-switch': deps/'opennow-switch',
    }
    for top in overrides.iterdir():
        if not top.is_dir(): continue
        if top.name in roots:
            destroot=roots[top.name]
        else:
            # Existing handoff overrides currently use opus/ and jansson/. Keep
            # this explicit failure so a future override cannot silently land
            # in the wrong dependency tree.
            raise SystemExit('unknown dependency override root: %s' % top.name)
        if not destroot.exists():
            raise SystemExit('dependency is missing for override %s: %s' % (top.name,destroot))
        for src in top.rglob('*'):
            if not src.is_file(): continue
            rel=src.relative_to(top)
            dst=destroot/rel
            dst.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(str(src),str(dst))
            copied += 1
            print('override', dst)
    print('Applied %d dependency override file(s).' % copied)
if __name__=='__main__': main()
