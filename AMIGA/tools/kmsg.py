"""Pre-renders kernel message bitmaps (1 bitplane, 320 wide) into build/gen/kmsg.h."""
import sys, os
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfxconv

def render(text, h=16, scale=2):
    font = ImageFont.load_default()
    im = Image.new('1', (160, h // scale), 0)
    d = ImageDraw.Draw(im)
    w = d.textlength(text, font=font)
    d.text(((160 - w) // 2, 0), text, fill=1, font=font)
    im = im.resize((320, h), Image.NEAREST)
    rows = [[im.getpixel((x, y)) for x in range(320)] for y in range(h)]
    return gfxconv.planes(rows, 1, width=320)[0]

out = sys.argv[1]
H = gfxconv.Header('kmsg.h')
H.array('kmsg_disk2', render('PLEASE INSERT DISK 2'))
H.array('kmsg_disk1', render('PLEASE INSERT DISK 1'))
H.define('KMSG_H', 16)
H.write(out)
