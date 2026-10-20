"""Render docs/wiring.svg - the interface between the lock bus and the ESP.

This is the receive stage of the original Fuhr controller, copied verbatim:
a 220k/47k divider into the base of a BC847B, emitter to ground, collector
pulled up to the logic rail. Reading the controller's own schematic settled
two things at once - that the bus is open-collector and idles high, and that
no opto-coupler is needed to sit on it.

    python3 docs/make_wiring.py
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wiring.svg')

W, H = 1040, 560
BG = '#ffffff'
WIRE = '#1f2328'
TXT = '#1f2328'
MUTED = '#57606a'
BLUE = '#0969da'
GREEN = '#1a7f37'
RED = '#cf222e'

out = []


def add(s):
    out.append(s)


def line(x1, y1, x2, y2, colour=WIRE, w=1.6, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ''
    add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" '
        'stroke-width="%.1f"%s stroke-linecap="round"/>'
        % (x1, y1, x2, y2, colour, w, d))


def poly(pts, colour=WIRE, w=1.6, fill='none'):
    d = ' '.join('%s%.1f,%.1f' % ('M' if i == 0 else 'L', p[0], p[1])
                 for i, p in enumerate(pts))
    add('<path d="%s" fill="%s" stroke="%s" stroke-width="%.1f" '
        'stroke-linecap="round" stroke-linejoin="round"/>' % (d, fill, colour, w))


def text(x, y, s, size=12, colour=TXT, anchor='start', bold=False):
    add('<text x="%.1f" y="%.1f" font-size="%d" fill="%s" text-anchor="%s"%s>%s</text>'
        % (x, y, size, colour, anchor, ' font-weight="600"' if bold else '',
           s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')))


def dot(x, y, colour=WIRE):
    add('<circle cx="%.1f" cy="%.1f" r="3.4" fill="%s"/>' % (x, y, colour))


def res_h(x, y, ref, val, ln=58, hh=12):
    add('<rect x="%.1f" y="%.1f" width="%d" height="%d" fill="%s" stroke="%s" '
        'stroke-width="1.6" rx="2"/>' % (x - ln / 2, y - hh, ln, hh * 2, BG, WIRE))
    text(x, y - hh - 7, ref, 11, MUTED, 'middle')
    text(x, y + hh + 15, val, 11, TXT, 'middle', bold=True)


def res_v(x, y, ref, val, ln=58, hh=12):
    add('<rect x="%.1f" y="%.1f" width="%d" height="%d" fill="%s" stroke="%s" '
        'stroke-width="1.6" rx="2"/>' % (x - hh, y - ln / 2, hh * 2, ln, BG, WIRE))
    text(x + hh + 7, y - 2, ref, 11, MUTED)
    text(x + hh + 7, y + 13, val, 11, TXT, bold=True)


def gnd(x, y, label=None, colour=WIRE):
    line(x, y - 14, x, y, colour)
    for i, wd in enumerate((18, 11, 5)):
        line(x - wd / 2, y + i * 5, x + wd / 2, y + i * 5, colour, 2.2)
    if label:
        text(x, y + 30, label, 10, MUTED, 'middle')


def supply(x, y, label, colour):
    line(x, y, x, y + 16, colour, 2)
    poly([(x - 6, y + 7), (x, y), (x + 6, y + 7)], colour, 2)
    text(x, y - 7, label, 11, colour, 'middle', bold=True)


def npn(x, y, ref, part):
    """NPN: base left, collector up, emitter down."""
    line(x - 42, y, x - 16, y)                    # base lead
    line(x - 16, y - 20, x - 16, y + 20, WIRE, 2.4)   # base bar
    line(x - 16, y - 10, x, y - 26)               # to collector
    line(x, y - 26, x, y - 52)
    line(x - 16, y + 10, x, y + 26)               # to emitter
    line(x, y + 26, x, y + 52)
    poly([(x - 9, y + 14), (x, y + 26), (x - 3, y + 13)], WIRE, 1.4, WIRE)  # arrow out
    add('<circle cx="%.1f" cy="%.1f" r="27" fill="none" stroke="%s" '
        'stroke-width="1.4"/>' % (x - 4, y, WIRE))
    text(x + 26, y - 30, ref, 11, TXT, bold=True)
    text(x + 26, y - 16, part, 10, MUTED)
    text(x - 46, y - 8, 'B', 10, MUTED, 'end')
    text(x + 6, y - 46, 'C', 10, MUTED)
    text(x + 6, y + 50, 'E', 10, MUTED)


# ---------------------------------------------------------------- canvas
add('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" '
    'font-family="ui-sans-serif,-apple-system,Segoe UI,Helvetica,Arial,sans-serif">'
    % (W, H))
add('<rect width="%d" height="%d" fill="%s"/>' % (W, H, BG))

text(34, 34, "Reading the bus the way the controller itself does", 17, TXT, bold=True)
text(34, 55, "The receive stage of the original Fuhr controller, component for "
             "component. The bus sees one 220 k resistor into a base — about "
             "50 µA.", 12, MUTED)

YS = 300            # the signal centreline
XB = 430            # transistor

# --- tap terminals ---------------------------------------------------------
add('<rect x="48" y="238" width="104" height="124" fill="%s" stroke="%s" '
    'stroke-width="1.6" rx="4"/>' % (BG, WIRE))
text(100, 258, 'Lock bus', 11, MUTED, 'middle')
text(142, 304, 'data', 12, BLUE, 'end', bold=True)
line(152, YS, 176, YS, BLUE, 2.4)
text(142, 344, 'GND', 12, WIRE, 'end', bold=True)
line(152, 340, 200, 340)
gnd(200, 340, 'lock GND')

# --- divider into the base -------------------------------------------------
dot(176, YS, BLUE)
line(176, YS, 206, YS)
res_h(240, YS, 'R1', '220k')
line(269, YS, 330, YS)
dot(330, YS)
line(330, YS, 330, YS + 26)
res_v(330, YS + 60, 'R2', '47k')
line(330, YS + 89, 330, YS + 104)
gnd(330, YS + 104)
line(330, YS, XB - 42, YS)

# --- transistor ------------------------------------------------------------
npn(XB, YS, 'Q1', 'BC847B')
line(XB, YS + 52, XB, YS + 104)
gnd(XB, YS + 104)

# --- collector: pull-up and the GPIO --------------------------------------
line(XB, YS - 52, XB, YS - 96)
dot(XB, YS - 96)
supply(XB, YS - 216, '+3.3 V (ESP)', GREEN)
line(XB, YS - 200, XB, YS - 182, GREEN, 2)
res_v(XB, YS - 153, 'R3', '10k')
line(XB, YS - 124, XB, YS - 96)

line(XB, YS - 96, 620, YS - 96)
line(620, YS - 96, 620, YS)
line(620, YS, 650, YS)
res_h(686, YS, 'R4', '100R', ln=50)
line(711, YS, 812, YS)
dot(812, YS, GREEN)
text(808, YS - 12, 'GPIO16', 11, GREEN, 'end', bold=True)

# --- ESP block -------------------------------------------------------------
add('<rect x="828" y="196" width="150" height="248" fill="%s" stroke="%s" '
    'stroke-width="1.8" rx="8"/>' % (BG, GREEN))
text(903, 232, 'ESP32', 14, GREEN, 'middle', bold=True)
text(903, 252, 'ESPHome', 10, MUTED, 'middle')
text(903, 272, 'fuhr_multitronic', 10, MUTED, 'middle')
line(812, YS, 828, YS, GREEN, 2)
line(828, 400, 812, 400, GREEN, 2)
line(812, 400, 812, 430)
gnd(812, 430, 'shared with lock GND', GREEN)
text(903, 408, 'own 5 V supply', 10, MUTED, 'middle')

# --- annotations -----------------------------------------------------------
text(176, 196, 'idles HIGH (~11 V), pulled LOW to send', 11, BLUE)
line(176, 204, 176, YS - 10, BLUE, 1.2, dash='4 3')
text(486, 168, 'idles LOW, pulses HIGH — exactly what', 11, GREEN)
text(486, 182, 'the PIC sees, so  inverted: false', 11, GREEN)
line(620, 190, 620, YS - 96, GREEN, 1.2, dash='4 3')

text(34, 498, 'No opto-coupler and no 12 V rail needed: the stage runs entirely '
              'off the ESP’s own 3.3 V, and the bus only ever drives a base.',
     12, TXT)
text(34, 520, 'The grounds are shared, which the original does too. Keep the ESP '
              'on its own supply so it outlives a cut to the lock.', 12, MUTED)

add('</svg>')

with open(OUT, 'w', encoding='utf-8') as fh:
    fh.write('\n'.join(out) + '\n')
print('wrote', OUT)
