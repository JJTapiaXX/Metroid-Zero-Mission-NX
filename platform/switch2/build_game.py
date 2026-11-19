#!/usr/bin/env python3
"""Build the Switch 2 Kaihen/OunceVK target using the shared game builder."""
from pathlib import Path
import runpy
import sys

if __name__ == '__main__':
    sys.argv[1:1] = ['--target', 'switch2']
    shared=Path(__file__).resolve().parents[1]/'switch'
    sys.path.insert(0,str(shared))
    runpy.run_path(str(shared/'build_game.py'), run_name='__main__')
