"""Original Aster compass icon; generated locally, no external artwork."""
from pathlib import Path
from PIL import Image, ImageDraw
out=Path(__file__).resolve().parents[1]/'src'/'app'/'aster.ico'
im=Image.new('RGBA',(256,256),(9,10,22,255));draw=ImageDraw.Draw(im)
draw.rounded_rectangle((8,8,248,248),radius=58,fill=(13,15,30,255),outline=(57,54,86,255),width=3)
draw.ellipse((46,46,210,210),outline=(94,60,180,255),width=4)
draw.polygon([(128,28),(150,105),(228,128),(150,151),(128,228),(106,151),(28,128),(106,105)],fill=(138,91,255,255))
draw.polygon([(128,70),(141,115),(186,128),(141,141),(128,186),(115,141),(70,128),(115,115)],fill=(223,211,255,255))
draw.ellipse((117,117,139,139),fill=(13,15,30,255))
im.save(out,format='ICO',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print('Original application icon generated.')
