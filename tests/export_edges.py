"""Regenerate the test fixtures from the raw sigrok captures.

    python3 export_edges.py /path/to/captures

Writes one file per capture into edges/ with lines "t_us bus ctrl", plus
expected.txt listing the reference message codes.

The raw .sr captures are NOT part of this repository - they are several
hundred megabytes of logic data. The generated fixtures in edges/ are
committed instead, so the test suite runs without them. You only need this
script when adding new captures.

The capture directory must contain the .sr files, plus srlib.py and
decoded.json from the protocol analysis that produced them.

The capture files keep their original Dutch names; the test cases they produce
are named in English via TEST_NAMES below. A capture that is not listed there
falls back to a sanitised version of its own filename, so newly recorded
captures still work without editing this file.
"""
import os, sys, json, glob

HERE = os.path.dirname(os.path.abspath(__file__))
CAPS = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.getcwd()
sys.path.insert(0, CAPS)

try:
    import numpy as np
    import srlib  # noqa: E402
except ImportError as exc:
    sys.exit('%s\nRun as: python3 export_edges.py /path/to/captures '
             '(the directory holding the .sr files, srlib.py and '
             'decoded.json)' % exc)

# Original capture filename (without .sr) -> English test case name.
TEST_NAMES = {
    '01-01 alle schoten blijven ingetrokken stoppen, daarna error om deur op slot te kunnen doen.':
        'stop_all_bolts_retracted_then_error_when_locking',
    '01-01 controller verliest verbinding met slot, schakelt binnen 21 seconden na laaste statusupdate naar deur niet op slot (Schakelt relay)':
        'controller_loses_contact_reports_unlocked_after_21s',
    '01-01 controller verliest verbinding met slot, schakelt deurcontact op deur geopend':
        'controller_loses_contact_sets_door_contact_open',
    '01-01 ingang 9+10 lang, alle schoten blijven ingetrokken. aantal keer deur open en dicht.':
        'input_9_10_held_bolts_retracted_door_cycled',
    '01-01 ingang 9+10 lang, alle schoten blijven ingetrokken. deur niet geopend, daarna 9+10 weer laag.':
        'input_9_10_held_bolts_retracted_door_not_opened_then_released',
    '02-01 deur in dagstand, handmatig deur op slot doen en van slot meerdere keren':
        'day_mode_manual_lock_unlock_repeated',
    '02-01 deur in dagstand, volledig elektrisch openen, 1e keer is deur direct opengeklikt, 2e keer deur niet geopend, 3e keer wel geopend na 2 seconden nadat dagschoot is ingetrokken':
        'day_mode_full_electric_open_three_attempts',
    '28-12 deur handmatig van slot halen, wachten, automatisch op slot':
        'manual_unlock_wait_auto_relock',
    '28-12 deur in dagstand, slot elektrisch openen met dagschoot, deur dicht, 3x, bij laatste deur later geopend':
        'day_mode_electric_open_day_bolt_3x_last_opened_later',
    '28-12 slot met dagschoot openen, 1s, deur openen, dagschoot vrijgegeven, 4s, deur sluiten':
        'open_with_day_bolt_1s_pulse_open_close_door_28_12',
    '30-12 dagstand al aan, dagstand uitschakelen, slot blokkeert':
        'day_mode_already_on_switch_off_lock_blocks',
    '30-12 dagstand inschakelen , storing deur niet van slot':
        'day_mode_on_fault_door_not_unlocked',
    '30-12 dagstand inschakelen , storing deur niet van slot 2':
        'day_mode_on_fault_door_not_unlocked_2',
    # This capture was originally named "dagstand inschakelen"; it carries
    # command 0x13 (day mode OFF), so the file was renamed to match.
    '30-12 dagstand uitschakelen , storing deur niet van slot, deur openen':
        'day_mode_off_fault_door_not_unlocked_then_door_opened',
    '30-12 dagstand inschakelen, even later motorstoring - alarm klopt niet....':
        'day_mode_on_then_motor_fault_alarm_incorrect',
    '30-12 dagstand uitschakelen, storing deur niet van slot 3.pvs':
        'day_mode_off_fault_door_not_unlocked_3',
    '30-12 geen dagstand, blokkade bij dichtdraaien slot':
        'no_day_mode_blockage_while_locking',
    'dagstand aan':
        'day_mode_on',
    'dagstand inschakelen, deur 5x open en dicht, dagstand uitschakelen':
        'day_mode_on_door_cycled_5x_day_mode_off',
    'dagstand uit':
        'day_mode_off',
    'lange sessie voor status ophalen, dagstand inschakelen en statussen':
        'long_session_day_mode_on_and_statuses',
    'lange sessie voor status ophalen, dagstand uit':
        'long_session_day_mode_off_keepalive_only',
    'slot met dagschoot openen, 1s, deur openen, dagschoot vrijgegeven, 4s, deur sluiten':
        'open_with_day_bolt_1s_pulse_open_close_door',
    'slot openen met dagschoot, deur openen, magneet ertegen, deur op slot, magneet weg 2x, ERROR, slot openen met dagschoot, deur sluiten':
        'open_with_day_bolt_magnet_on_contact_tamper_error',
    'slot openen met dagschoot, deur openzwaaien, deur dichtmaken, automatisch slot vergrendeld.':
        'open_with_day_bolt_swing_door_open_close_auto_lock',
    # Added after the first round of analysis, to settle the open questions
    # about the TGF jumper, the input ports and code 0x18.
    '9+10 lang worden alle schoten volledig ingetrokken, inclusief':
        'input_9_10_continuous_all_bolts_retracted_incl_day_bolt',
    '11+12  tgf uit, dagfunctie 2 worden alle schoten volledig ingetrokken, dagschoot niet':
        'input_11_12_tgf_removed_function_2_day_bolt_stays_out',
    'TGF in — dagstand in, 20 s wachten, dagstand uit (pin11+12)':
        'tgf_fitted_day_mode_on_wait_20s_then_off',
    'TGF uit — dagstand in, 20 s wachten, dagstand uit (pin11+12)':
        'tgf_removed_day_mode_on_wait_20s_then_off',
    'alarm - deur op slot, deur openen terwijl vergrendeld, 40s wachten, deur gesloten':
        'alarm_door_forced_while_locked_wait_40s_then_closed',
}


def test_name(capture_name):
    name = TEST_NAMES.get(capture_name)
    if name is None:
        print('warning: no English test name for %r, using filename'
              % capture_name, file=sys.stderr)
        name = ''.join(c if c.isalnum() else '_' for c in capture_name)[:60]
    return name


def main():
    outdir = os.path.join(HERE, 'edges')
    os.makedirs(outdir, exist_ok=True)
    ref = {c['name']: c for c in json.load(open(os.path.join(CAPS, 'decoded.json')))}
    index = []
    for f in sorted(glob.glob(os.path.join(CAPS, '*.sr'))):
        name = os.path.basename(f)[:-3]
        s = srlib.read_sr(f)
        st, vl, total = srlib.stream_edges(s, 0x03)
        fs = s['samplerate']
        us = (st.astype(np.float64) * 1e6 / fs).astype(np.int64)
        bus = (vl >> 0) & 1
        ctrl = (vl >> 1) & 1
        safe = test_name(name)
        with open(os.path.join(outdir, safe + '.txt'), 'w') as fh:
            for t, b, c in zip(us, bus, ctrl):
                fh.write('%d %d %d\n' % (t, b, c))
        codes = [int(fr['payload'], 16) >> 8
                 for fr in ref[name]['frames'] if fr['type'] == 'data']
        acks = sum(1 for fr in ref[name]['frames'] if fr['type'] != 'data')
        index.append('%s %d %d %s' % (safe, len(codes), acks,
                                      ','.join('%02X' % c for c in codes)))
    with open(os.path.join(HERE, 'expected.txt'), 'w') as fh:
        fh.write('\n'.join(index) + '\n')
    print('exported %d captures' % len(index))


if __name__ == '__main__':
    main()
