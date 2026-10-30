"""Play hum-like tones and speech to validate the game's actual mic controls."""
import array
import math
import os
import re
import subprocess
import tempfile
import time
import wave

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app_xghostbusters")
IMAGE = os.path.join(HERE, "bin/xghostbusters.xe")
TEMP = tempfile.gettempdir()


def load():
    subprocess.run(["xrun", "--adapter-id", "94R8235L", "--noreset", IMAGE], check=True)
    time.sleep(1.3)


def snapshot():
    result = subprocess.run([
        "xgdb", "--batch", "-ex", "attach --adapter-id 94R8235L",
        "-ex", "print audio_diag.hum_hz", "-ex", "print game_diag",
        "-ex", "print capture_overruns", "-ex", "print display_frame_ticks",
        "-ex", "print audio_diag", "-ex", "print voice_diag",
        "-ex", "print controls_diag",
        "-ex", "detach", IMAGE,
    ], check=True, text=True, capture_output=True)
    print(result.stdout, flush=True)
    assert "$3 = 0" in result.stdout, "Audio queue overrun"
    assert int(re.search(r"\$4 = (\d+)", result.stdout)[1]) < 3333333, "Render missed 30 fps budget"
    return result.stdout


for hz, lane in ((120, 2), (220, 1), (330, 0), (0, 0)):
    load()
    with tempfile.NamedTemporaryFile(suffix=".wav", dir=TEMP) as output:
        # Last trial slides from low to high with harmonics and vibrato.
        phase = 0
        samples = array.array("h")
        for i in range(48000 * 4):
            t = i / 48000
            frequency = hz or (120 + 210 * min(1, t / 1.5) + 3 * math.sin(2 * math.pi * 5 * t))
            phase += 2 * math.pi * frequency / 48000
            samples.append(int(8000 * math.sin(phase) + 3000 * math.sin(2 * phase)))
        with wave.open(output.name, "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(48000)
            wav.writeframes(samples.tobytes())
        player = subprocess.Popen(["afplay", output.name])
        try:
            time.sleep(1 if hz else 2)
            data = snapshot()
            detected = int(re.search(r"\$1 = (\d+)", data)[1])
            assert abs(detected - (hz or 330)) <= 6, (hz, detected)
            assert int(re.search(r"lives = \d+, lane = (\d+)", data)[1]) == lane
            assert "voice = 0" in data and "hum = 1" in data, "Hum should move without firing"
            assert not re.search(r"shots = .*active = 1", data), "Hum fired a shot"
            assert "shots_fired = 0" in data, "Hum onset fired a shot"
        finally:
            player.wait()
        time.sleep(0.15)
        data = snapshot()
        assert "voice = 0" in data, "VAD hangover fired after the hum"

load()
speaker = subprocess.Popen(["say", "-r", "155", "Ghostbusters, fire the proton pack. Catch the ghosts. Who are you going to call?"])
try:
    time.sleep(1.5)
    data = snapshot()
    # Speech contains vowels and pauses that deliberately inhibit firing, so
    # inspect cumulative shots rather than requiring fire at one arbitrary hop.
    assert int(re.search(r"shots_fired = (\d+)", data)[1]) > 0, "Speech produced no proton shots"
finally:
    speaker.wait()
print("PASS: acoustic hum lanes without firing, quiet release, and speech-driven proton shots")
