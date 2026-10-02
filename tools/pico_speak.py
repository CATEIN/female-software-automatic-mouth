"""Talk to the Pico and hear it through the computer's speakers.

    pip install pyserial pyaudio      # once
    python tools/pico_speak.py [COM5] [--wav out.wav]

The Pico does the speech synthesis; this program sends it your text over the
USB cable, receives the finished 22050 Hz samples and plays them. It puts the
sketch in USB audio mode ("#usb 1") and back to PWM ("#usb 0") on exit.
Type text or the sketch's # commands (#1 Sadie, #2 Sadie '82, #3 SAM,
#4 Male, #? for the rest). Ctrl+C or an empty line + "quit" exits.

Without a port argument it picks the first Raspberry Pi Pico it finds.
--wav also records everything played into a WAV file.
"""
import queue, struct, sys, threading, wave

RATE = 22050
PICO_VID = 0x2E8A


class PacketReader:
    """Splits the Pico's serial stream into text and audio packets
    (0x01, count uint16 LE, count int16 samples; count 0 ends a phrase)."""

    def __init__(self, on_text, on_audio, on_end):
        self.on_text, self.on_audio, self.on_end = on_text, on_audio, on_end
        self.buf = bytearray()

    def feed(self, data):
        self.buf += data
        while self.buf:
            if self.buf[0] != 1:
                end = self.buf.find(b"\x01")
                end = len(self.buf) if end < 0 else end
                self.on_text(bytes(self.buf[:end]))
                del self.buf[:end]
                continue
            if len(self.buf) < 3:
                return
            n = self.buf[1] | self.buf[2] << 8
            if len(self.buf) < 3 + 2 * n:
                return
            if n:
                self.on_audio(bytes(self.buf[3:3 + 2 * n]))
            else:
                self.on_end()
            del self.buf[:3 + 2 * n]


def find_port():
    from serial.tools import list_ports
    for p in list_ports.comports():
        if p.vid == PICO_VID:
            return p.device
    ports = [p.device for p in list_ports.comports()]
    sys.exit("No Raspberry Pi Pico found (ports: %s). Give the port, e.g. python tools/pico_speak.py COM5"
             % (", ".join(ports) or "none"))


def main():
    import serial, pyaudio

    args = sys.argv[1:]
    wav_path = None
    if "--wav" in args:
        i = args.index("--wav")
        wav_path = args[i + 1]
        del args[i:i + 2]
    port = args[0] if args else find_port()

    ser = serial.Serial(port, 115200, timeout=0.05)
    pa = pyaudio.PyAudio()
    out = pa.open(format=pyaudio.paInt16, channels=1, rate=RATE, output=True, frames_per_buffer=1024)
    wav = None
    if wav_path:
        wav = wave.open(wav_path, "wb")
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)

    audio = queue.Queue()
    running = True

    def player():
        # the Pico runs faster than real time, so this queue only fills up
        while running:
            try:
                data = audio.get(timeout=0.1)
            except queue.Empty:
                continue
            out.write(data)
            if wav:
                wav.writeframes(data)

    def text(data):
        sys.stdout.write(data.decode("latin-1"))
        sys.stdout.flush()

    reader = PacketReader(text, audio.put, lambda: None)

    def receiver():
        while running:
            data = ser.read(4096)
            if data:
                reader.feed(data)

    threading.Thread(target=player, daemon=True).start()
    threading.Thread(target=receiver, daemon=True).start()

    print(f"Connected to the Pico on {port}. Type text to speak it, #? for commands, quit to exit.")
    ser.write(b"#usb 1\n")
    try:
        while True:
            line = input()
            if line.strip().lower() in ("quit", "exit"):
                break
            if line.strip():
                ser.write(line.encode("latin-1", "replace") + b"\n")
    except (KeyboardInterrupt, EOFError):
        pass
    ser.write(b"#usb 0\n")
    while not audio.empty():            # let the last phrase finish
        threading.Event().wait(0.1)
    running = False
    threading.Event().wait(0.3)
    out.stop_stream()
    out.close()
    pa.terminate()
    ser.close()
    if wav:
        wav.close()
        print("saved", wav_path)


if __name__ == "__main__":
    main()
