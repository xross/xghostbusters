"""Acoustic smoke test: quiet, synthetic speech, then quiet recovery."""
import os
import re
import subprocess
import time

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app_xghostbusters")


def snapshot(label):
    result = subprocess.run([
        "xgdb", "--batch", "-ex", "attach --adapter-id 94R8235L",
        "-ex", "print voice_diag", "-ex", "print capture_overruns",
        "-ex", "print capture_second_ticks", "-ex", "print audio_diag.self_test_pass",
        "-ex", "detach", os.path.join(HERE, "bin/xghostbusters.xe")
    ], check=True, capture_output=True, text=True)
    print(label + "\n" + result.stdout, flush=True)
    active = int(re.search(r"active = (\d+)", result.stdout)[1])
    frames = int(re.search(r"speech_frames = (\d+)", result.stdout)[1])
    assert "$2 = 0" in result.stdout, "Audio receive queue overrun"
    assert "$3 = 100000000" in result.stdout, "Unexpected sample cadence"
    assert "$4 = 1" in result.stdout, "FFT self-test failed"
    return active, frames


time.sleep(2)
_, before = snapshot("Before speech")
speaker = subprocess.Popen([
    "say", "-r", "155",
    "This is a live voice detection test. The microphone should detect speech "
    "while the frequency spectrum continues to move. We are testing voice "
    "activity on the X core processor. This sentence continues the voice detection test."
])
try:
    time.sleep(2)
    active, during = snapshot("During synthetic speech")
    assert active == 1 and during > before, "Speech was not detected"
finally:
    speaker.wait()
time.sleep(2)
for attempt in range(4):
    active, after = snapshot("After speech")
    if active == 0:
        break
    time.sleep(1)
assert active == 0, "Indicator did not return to QUIET"
assert after > before
print("PASS: speech detected, quiet recovered, no receive overruns.")
