#!/usr/bin/env python3
"""U2E (second vector part, the city) data."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import visu2c

out = sys.argv[1]
visu2c.convert('U2E', 'u2e', out, ncolors=31, color_base=1)
