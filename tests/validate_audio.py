"""Play short tones on the host and inspect the running board through XTAG."""
import array
import math
import os
import re
import subprocess
import tempfile
import time
import wave

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app_xghostbusters")
TEMP = tempfile.gettempdir()
for frequency in (500, 1000, 2000):
    with tempfile.NamedTemporaryFile(suffix=".wav", dir=TEMP) as output:
        samples = array.array("h", (
            int(12000 * math.sin(2 * math.pi * frequency * i / 48000))
            for i in range(48000 * 4)
        ))
        with wave.open(output.name, "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(48000)
            wav.writeframes(samples.tobytes())
        player = subprocess.Popen(["afplay", output.name])
        time.sleep(1.5)
        print(f"\nApplied acoustic tone: {frequency} Hz", flush=True)
        result = subprocess.run([
            "xgdb", "--batch", "-ex", "attach --adapter-id 94R8235L",
            "-ex", "print audio_diag", "-ex", "print capture_second_ticks",
            "-ex", "print capture_overruns", "-ex", "print display_frames",
            "-ex", "detach", os.path.join(HERE, "bin/xghostbusters.xe")
        ], check=True, text=True, capture_output=True)
        print(result.stdout)
        detected = int(re.search(r"dominant_hz = (\d+)", result.stdout)[1])
        assert abs(detected-frequency) <= 16, (frequency, detected)
        assert "self_test_pass = 1" in result.stdout
        player.wait()
print("PASS: all three live acoustic tones detected within one FFT bin.")
