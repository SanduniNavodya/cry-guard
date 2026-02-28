import os
import wave
import struct
import math
import random

def create_dummy_wav(filename, duration, sample_rate, base_freq):
    """
    Creates a dummy WAV file with a sine wave tone and some noise.
    """
    num_samples = int(duration * sample_rate)
    
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        
        for i in range(num_samples):
            # Add a bit of frequency variation/noise
            freq = base_freq + random.uniform(-10, 10)
            value = int(10000.0 * math.sin(2.0 * math.pi * freq * i / sample_rate))
            data = struct.pack('<h', value)
            wav_file.writeframesraw(data)

def generate_dataset():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    crying_dir = os.path.join(base_dir, "data", "crying")
    not_crying_dir = os.path.join(base_dir, "data", "not_crying")
    
    os.makedirs(crying_dir, exist_ok=True)
    os.makedirs(not_crying_dir, exist_ok=True)
    
    # Generate 10 dummy "crying" samples (higher pitch tone)
    print(f"Generating dummy 'crying' data in {crying_dir}...")
    for i in range(10):
        filename = os.path.join(crying_dir, f"dummy_cry_{i}.wav")
        create_dummy_wav(filename, duration=1.0, sample_rate=16000, base_freq=800.0)
        
    # Generate 10 dummy "not_crying" samples (lower pitch tone)
    print(f"Generating dummy 'not_crying' data in {not_crying_dir}...")
    for i in range(10):
        filename = os.path.join(not_crying_dir, f"dummy_not_cry_{i}.wav")
        create_dummy_wav(filename, duration=1.0, sample_rate=16000, base_freq=200.0)

if __name__ == "__main__":
    generate_dataset()
    print("Dummy dataset generated successfully! You can now run train_model.py")
