"""XTAG renderer timing smoke test. Injects scenes, then reloads the title screen.

Run from any directory with the board connected and XMOS tools on PATH.
No speaker or acoustic input is needed. Times are 100 MHz reference ticks.
"""
from pathlib import Path
import re
import subprocess
import time

IMAGE = Path(__file__).resolve().parents[1] / "app_xghostbusters/bin/xghostbusters.xe"
ADAPTER = "94R8235L"


def debugger(commands):
    args = ["xgdb", "--batch", "-ex", "attach --adapter-id " + ADAPTER]
    for command in commands:
        args.extend(["-ex", command])
    args.extend(["-ex", "detach", str(IMAGE)])
    result = subprocess.run(args, check=True, text=True, capture_output=True, timeout=30)
    if re.search(r"No symbol|Cannot |not available|optimized out|Error", result.stderr):
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def inject(assignments):
    debugger(["break game_step", "continue"]
             + ["set g->" + assignment for assignment in assignments]
             + ["delete breakpoints"])


def measure(label, duration):
    # Discard the initial full-screen transition and any debugger-resume frame.
    time.sleep(0.25)
    debugger(["set display_reset_stats = 1", "set capture_overruns = 0"])
    time.sleep(duration)
    output = debugger(["print display_peak_ticks", "print display_budget_misses",
                       "print capture_overruns", "print game_diag.over"])
    values = [int(x) for x in re.findall(r"\$\d+ = (\d+)", output)]
    assert len(values) == 4, output
    peak, missed, overruns, over = values
    print(f"{label}: peak {peak / 100000:.2f} ms, misses {missed}, audio overruns {overruns}")
    assert peak and peak < 3333333 and not missed and not overruns, output
    assert not over, "Scenario ended before it was measured"


def reload():
    subprocess.run(["xrun", "--adapter-id", ADAPTER, "--noreset", str(IMAGE)], check=True)
    time.sleep(2)


def main():
    reload()
    try:
        measure("title", 0.7)
        inject(["title = 0", "frame = 100", "spawn = 600", "invincible = 600",
                "boss_active = 1", "boss_frame = 0", "boss_hp = 18", "score = 1000"])
        measure("Stay Puft entrance", 0.7)
        inject(["boss_frame = 90", "boss_attack = 89", "boss_hp = 7",
                "shots[0].x = 125", "shots[0].lane = 1", "shots[0].active = 1"])
        measure("Stay Puft attacks", 0.8)
        inject(["boss_active = 0", "boss_next = 999999", "spawn = 600",
                "hazards[0].active = 0", "hazards[1].active = 0", "hazards[2].active = 0",
                "effects[0].x = 140", "effects[0].lane = 0", "effects[0].age = 1",
                "effects[0].kind = 0", "effects[1].x = 160", "effects[1].lane = 1",
                "effects[1].age = 1", "effects[1].kind = 1", "effects[2].x = 180",
                "effects[2].lane = 2", "effects[2].age = 1", "effects[2].kind = 2"])
        measure("three simultaneous traps", 0.45)
        inject(["boss_active = 1", "boss_frame = 90", "boss_hp = 0", "boss_defeat = 1"])
        measure("Stay Puft marshmallow burst", 0.8)
    finally:
        reload()
    print("PASS: animated scenes meet the 30 fps work budget; title screen restored")


if __name__ == "__main__":
    main()
