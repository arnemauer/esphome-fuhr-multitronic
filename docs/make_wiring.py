"""Render docs/wiring.svg - the interface between the lock bus and the ESP.

Two identical opto-isolated taps. The bus itself only ever sees a 147 k
divider (about 80 uA); the opto LED current is drawn from the lock's 12 V
supply rail, not from the signal line. The ESP side is galvanically separate
and has its own supply, so cutting the lock's power silences the bus without
taking the ESP down with it.

    python3 docs/make_wiring.py
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wiring.svg')

W, H = 1140, 1010
BG = '#ffffff'
WIRE = '#1f2328'
TXT = '#1f2328'
MUTED = '#57606a'
RED = '#cf222e'
BLUE = '#0969da'
PURPLE = '#8250df'
GREEN = '#1a7f37'
ISO = '#8250df'

BARRIER_X = 566
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


def res_h(x, y, ref, val, ln=54, hh=12):
    add('<rect x="%.1f" y="%.1f" width="%d" height="%d" fill="%s" stroke="%s" '
        'stroke-width="1.6" rx="2"/>' % (x - ln / 2, y - hh, ln, hh * 2, BG, WIRE))
    text(x, y - hh - 7, ref, 11, MUTED, 'middle')
    text(x, y + hh + 15, val, 11, TXT, 'middle', bold=True)


def res_v(x, y, ref, val, ln=54, hh=12):
    add('<rect x="%.1f" y="%.1f" width="%d" height="%d" fill="%s" stroke="%s" '
        'stroke-width="1.6" rx="2"/>' % (x - hh, y - ln / 2, hh * 2, ln, BG, WIRE))
    text(x + hh + 7, y - 2, ref, 11, MUTED)
    text(x + hh + 7, y + 13, val, 11, TXT, bold=True)


def gnd(x, y, label, colour=WIRE):
    line(x, y - 14, x, y, colour)
    for i, wd in enumerate((18, 11, 5)):
        line(x - wd / 2, y + i * 5, x + wd / 2, y + i * 5, colour, 2.2)
    text(x, y + 30, label, 10, MUTED, 'middle')


def supply(x, y, label, colour):
    """Stub arrow pointing up."""
    line(x, y, x, y + 16, colour, 2)
    poly([(x - 6, y + 7), (x, y), (x + 6, y + 7)], colour, 2)
    text(x, y - 7, label, 11, colour, 'middle', bold=True)


def mosfet(x, y, ref):
    line(x - 40, y, x - 17, y)
    line(x - 17, y - 18, x - 17, y + 18, WIRE, 2.2)
    for dy in (-13, 0, 13):
        line(x - 9, y + dy - 6, x - 9, y + dy + 6, WIRE, 2.6)
    line(x - 9, y - 18, x, y - 18)
    line(x, y - 18, x, y - 48)
    line(x - 9, y + 18, x, y + 18)
    line(x, y + 18, x, y + 48)
    line(x - 9, y, x, y)
    poly([(x - 6, y - 5), (x + 1, y), (x - 6, y + 5)], WIRE, 1.4, WIRE)
    text(x + 12, y - 26, ref, 11, TXT, bold=True)
    text(x + 12, y - 12, '2N7000', 10, MUTED)
    text(x - 44, y - 8, 'G', 10, MUTED, 'end')
    text(x + 6, y - 44, 'D', 10, MUTED)
    text(x + 6, y + 46, 'S', 10, MUTED)


def opto(cx_led, cx_pt, ys, ref):
    """Opto-coupler straddling the isolation barrier. ys = signal centreline."""
    top, bot = ys - 152, ys - 18
    add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="none" '
        'stroke="%s" stroke-width="1.4" stroke-dasharray="5 4" rx="7"/>'
        % (cx_led - 62, top, (cx_pt + 66) - (cx_led - 62), bot - top, ISO))
    text(cx_led - 54, top + 18, ref, 11, ISO, bold=True)
    text(cx_led - 54, top + 32, 'PC817', 10, MUTED)

    # LED, anode up
    ay, cy = ys - 104, ys - 66
    poly([(cx_led - 13, ay), (cx_led + 13, ay), (cx_led, cy), (cx_led - 13, ay)], WIRE, 1.6)
    line(cx_led - 14, cy, cx_led + 14, cy, WIRE, 2.4)
    line(cx_led, top + 6, cx_led, ay)
    line(cx_led, cy, cx_led, bot - 6)
    for k in (0, 1):
        bx, by = cx_led + 19, ay + 10 + k * 11
        poly([(bx, by), (bx + 15, by - 7)], WIRE, 1.2)
        poly([(bx + 11, by - 7), (bx + 15, by - 7), (bx + 13, by - 3)], WIRE, 1.2)

    # phototransistor
    ty = ys - 85
    add('<circle cx="%.1f" cy="%.1f" r="23" fill="none" stroke="%s" '
        'stroke-width="1.4"/>' % (cx_pt, ty, WIRE))
    line(cx_pt - 13, ty - 13, cx_pt - 13, ty + 13, WIRE, 2.4)
    line(cx_pt - 13, ty - 7, cx_pt + 11, ty - 20)
    line(cx_pt - 13, ty + 7, cx_pt + 11, ty + 20)
    poly([(cx_pt + 2, ty + 11), (cx_pt + 11, ty + 20), (cx_pt + 1, ty + 20)], WIRE, 1.4, WIRE)
    line(cx_pt + 11, ty - 20, cx_pt + 11, top + 6)
    line(cx_pt + 11, ty + 20, cx_pt + 11, bot - 6)
    return cx_pt + 11


def channel(ys, tap, tap_colour, gpio, refs):
    r1, r2, r3, r4, r5, q, u = refs
    xled, xpt = 480, 620

    # --- divider into the gate -----------------------------------------
    text(96, ys + 4, tap, 12, tap_colour, 'end', bold=True)
    line(104, ys, 150, ys, tap_colour, 2.4)
    dot(150, ys, tap_colour)
    line(150, ys, 176, ys)
    res_h(212, ys, r1, '100k')
    line(248, ys, 320, ys)
    dot(320, ys)
    line(320, ys, 320, ys + 24)
    res_v(320, ys + 51, r2, '47k')
    line(320, ys + 78, 320, ys + 96)
    gnd(320, ys + 96, 'lock GND')
    line(320, ys, 400, ys)

    # --- MOSFET switches the LED ---------------------------------------
    mosfet(xled, ys, q)
    line(xled, ys + 48, xled, ys + 96)
    gnd(xled, ys + 96, 'lock GND')

    # --- 12 V feed through R3 into the LED ------------------------------
    supply(xled, ys - 240, '+12 V (lock)', RED)
    line(xled, ys - 224, xled, ys - 205, RED, 2)
    res_v(xled, ys - 178, r3, '3k3')
    line(xled, ys - 151, xled, ys - 146)

    u_out = opto(xled, xpt, ys, u)

    # --- ESP side --------------------------------------------------------
    supply(xpt + 11, ys - 240, '+3.3 V (ESP)', GREEN)
    line(u_out, ys - 224, u_out, ys - 205, GREEN, 2)
    res_v(u_out, ys - 178, r4, '4k7')
    line(u_out, ys - 151, u_out, ys - 146)
    line(u_out, ys - 24, u_out, ys + 96)
    gnd(u_out, ys + 96, 'ESP GND', GREEN)

    # collector node out to the GPIO
    node_y = ys - 146
    dot(u_out, node_y)
    line(u_out, node_y, 760, node_y)
    line(760, node_y, 760, ys)
    line(760, ys, 790, ys)
    res_h(826, ys, r5, '100R', ln=48)
    line(850, ys, 944, ys)
    dot(944, ys, GREEN)
    text(940, ys - 10, gpio, 11, GREEN, 'end', bold=True)


# ------------------------------------------------------------------ canvas
add('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" '
    'font-family="ui-sans-serif,-apple-system,Segoe UI,Helvetica,Arial,sans-serif">'
    % (W, H))
add('<rect width="%d" height="%d" fill="%s"/>' % (W, H, BG))

text(34, 34, 'Tapping the Fuhr multitronic bus without loading it', 17, TXT, bold=True)
text(34, 55, 'The bus sees a 147 k divider and nothing else — about 80 uA. '
             'The LED current comes from the lock supply, so the signal line is '
             'never the source.', 12, MUTED)

# isolation barrier
line(BARRIER_X, 84, BARRIER_X, 922, ISO, 1.4, dash='7 5')
add('<text transform="translate(%d,500) rotate(-90)" font-size="11" fill="%s" '
    'text-anchor="middle" font-weight="600">galvanic isolation — grounds stay '
    'separate</text>' % (BARRIER_X - 8, ISO))

text(34, 104, 'LOCK SIDE — 12 V domain', 12, MUTED, bold=True)
text(706, 104, 'ESP SIDE — 3.3 V domain, its own supply', 12, GREEN, bold=True)

channel(360, 'Bus', BLUE, 'GPIO16', ('R1', 'R2', 'R3', 'R4', 'R5', 'Q1', 'U1'))
channel(760, 'Ctrl TX', PURPLE, 'GPIO17', ('R6', 'R7', 'R8', 'R9', 'R10', 'Q2', 'U2'))

# ESP block
add('<rect x="960" y="296" width="148" height="532" fill="%s" stroke="%s" '
    'stroke-width="1.8" rx="8"/>' % (BG, GREEN))
text(1034, 332, 'ESP32', 14, GREEN, 'middle', bold=True)
text(1034, 352, 'ESPHome', 10, MUTED, 'middle')
text(1034, 376, 'fuhr_multitronic', 10, MUTED, 'middle')
text(1034, 790, 'separate 5 V supply', 10, MUTED, 'middle')
text(1034, 806, '(not from the lock)', 10, MUTED, 'middle')
line(944, 360, 960, 360, GREEN, 2)
line(944, 760, 960, 760, GREEN, 2)

text(34, 958, 'Both GPIOs idle HIGH — the opto only conducts while the bus is '
              'driven high — so set  inverted: true  on each pin.', 12, TXT)
text(34, 980, 'Never join lock GND to ESP GND: that separation is what keeps the '
              'tap passive and lets the ESP outlive a cut to the lock supply.',
     12, MUTED)

add('</svg>')

with open(OUT, 'w', encoding='utf-8') as fh:
    fh.write('\n'.join(out) + '\n')
print('wrote', OUT)
