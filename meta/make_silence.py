"""Generate a 3-second silent WAV for the banner sound.

The banner sound is what plays when you highlight the app on HOME menu.
A short silence works fine — we can swap in a proper sting later.
Format: 16-bit PCM mono, 22050 Hz (small file, 3DS-friendly).
"""
import struct

SAMPLE_RATE = 22050
DURATION_SEC = 3
NUM_SAMPLES = SAMPLE_RATE * DURATION_SEC
BYTES_PER_SAMPLE = 2
DATA_SIZE = NUM_SAMPLES * BYTES_PER_SAMPLE

with open('meta/silence.wav', 'wb') as f:
    # RIFF header
    f.write(b'RIFF')
    f.write(struct.pack('<I', 36 + DATA_SIZE))
    f.write(b'WAVE')
    # fmt chunk
    f.write(b'fmt ')
    f.write(struct.pack('<I', 16))       # subchunk size
    f.write(struct.pack('<H', 1))        # PCM format
    f.write(struct.pack('<H', 1))        # mono
    f.write(struct.pack('<I', SAMPLE_RATE))
    f.write(struct.pack('<I', SAMPLE_RATE * BYTES_PER_SAMPLE))
    f.write(struct.pack('<H', BYTES_PER_SAMPLE))
    f.write(struct.pack('<H', 16))       # bits per sample
    # data chunk
    f.write(b'data')
    f.write(struct.pack('<I', DATA_SIZE))
    f.write(b'\x00' * DATA_SIZE)

print(f'Wrote meta/silence.wav ({DURATION_SEC}s, {SAMPLE_RATE}Hz mono)')
