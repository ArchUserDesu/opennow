#!/usr/bin/env python3
from pathlib import Path
import argparse

def replace(path, old, new):
    p=Path(path); s=p.read_text()
    if old in s:
        p.write_text(s.replace(old,new))
        print('patched',p)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--src', required=True); a=ap.parse_args()
    root=Path(a.src).resolve(); peer=root/'opennow-switch'/'extern'/'libpeer'/'src'
    if not peer.exists(): raise SystemExit('libpeer not found')
    replace(peer/'ports.c', '#elif defined(__SWITCH__)', '#elif defined(__SWITCH__) || defined(_XBOX)')
    # Diagnostics are normally disabled, but make any explicitly enabled traces land beside the XEX.
    replace(peer/'sctp.c', 'sdmc:/switch/OpenNOWSwitch/signaling.log', 'game:\\\\signaling.log')
    replace(peer/'sctp.c', 'sdmc:/switch/OpenNOWSwitch/stream_trace.log', 'game:\\\\stream_trace.log')
    replace(peer/'sctp.c', 'sdmc:/switch/OpenNOWSwitch/input.log', 'game:\\\\input.log')

if __name__=='__main__': main()
