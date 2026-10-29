"""Compatibility entry point for the replacement stationary-floor regression."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('run_zoom_save_floor_test.py')),run_name='__main__')
