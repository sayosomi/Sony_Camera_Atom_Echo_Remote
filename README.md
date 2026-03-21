# AtomEcho ZV-E10 Remote

Turn an M5Stack Atom Echo into a Bluetooth shutter remote for the Sony ZV-E10.

This project is meant to be easy to flash and use:

- No filesystem upload step
- No extra app on the camera
- Spoken-style WAV cues play through the Atom Echo speaker
- Audio files are already embedded in the sketch

## What You Need

- M5Stack Atom Echo
- Sony ZV-E10 with Bluetooth remote control enabled
- USB cable for the Atom Echo
- Arduino IDE 2.x

## Arduino IDE Setup

These steps follow the current M5Stack Arduino documentation for Atom-series boards.

### 1. Add the M5Stack board package

In Arduino IDE:

1. Open `Arduino IDE > Settings`
2. Find `Additional boards manager URLs`
3. Add:

```text
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

4. Open `Tools > Board > Boards Manager`
5. Search for `M5Stack`
6. Install the `M5Stack` board package

### 2. Install the required library

In `Tools > Manage Libraries`:

1. Search for `M5Unified`
2. Install it
3. Accept any dependency prompts

### 3. Open the sketch

Open `AtomEcho_ZV-E10_Remote.ino` in Arduino IDE.

### 4. Select the correct board settings

Use these settings before uploading:

- Board: `M5Atom`
- Partition Scheme: `Huge APP`
- Port: select the Atom Echo serial port shown by Arduino IDE

`Huge APP` is required because the audio cues are compiled into flash.

### 5. Upload

Click the normal Arduino IDE Upload button.

## First Use

### Pair the camera

1. Enable Bluetooth remote control on the Sony ZV-E10
2. Put the camera into Bluetooth pairing mode
3. Power the Atom Echo and upload the sketch if you have not already
4. If the Atom Echo has no saved camera, it will play `pairing.wav`
5. Wait for the connection to complete
6. When pairing succeeds, it will play `connected.wav`

If a camera was already paired before, the Atom Echo will try to reconnect automatically and will play `connecting.wav` on boot.

## Button Behavior

- Connected, press `BtnA`: begin focus hold immediately
- Connected, release `BtnA`: play a random `shutter*.wav` cue and send the shutter command
- Not connected, short press `BtnA`: replay the current search cue
- Not connected, hold `BtnA` for 2 seconds: clear the saved bond and return to pairing mode

## LED Behavior

- Purple: no saved camera, waiting to pair
- Red: saved camera exists, not connected
- Green: connected and ready

## Troubleshooting

### The sketch does not fit

Make sure `Tools > Partition Scheme` is set to `Huge APP`.

### The board does not appear in Arduino IDE

- Try another USB cable
- Reconnect the Atom Echo
- Reopen the port menu

### The camera does not reconnect

Hold `BtnA` for 2 seconds while disconnected to clear the saved bond, then pair again.

## Advanced: Arduino CLI

Compile:

```bash
arduino-cli compile --fqbn m5stack:esp32:m5stack_atom:PartitionScheme=huge_app /Users/yosomi/Code/AtomEcho_ZV-E10_Remote
```

Upload:

```bash
arduino-cli upload -p /dev/cu.usbserial-0001 --fqbn m5stack:esp32:m5stack_atom:PartitionScheme=huge_app /Users/yosomi/Code/AtomEcho_ZV-E10_Remote
```

Replace the port with the current Atom Echo serial device on your machine.

## Advanced: Replace or Add Audio Cues

You only need this section if you want to customize the spoken cues. Normal flashing does not require it.

### Replace the standard cues

Replace any of these files in the repository root:

- `pairing.wav`
- `connecting.wav`
- `connected.wav`

### Add or replace shutter cues

- Keep shutter files in the repository root
- Name them `shutter0.wav`, `shutter1.wav`, `shutter2.wav`, and so on
- The script embeds every `shutter*.wav` file in numeric order
- If `shutter0.wav` exists, it is skipped on the first shot after boot

### Regenerate the embedded headers

After changing the WAV files, run:

```bash
./scripts/regenerate-audio-headers.sh
```

The script rewrites:

- `AudioAssets.h`
- `ShutterVariants.h`

Then rebuild and upload the sketch again.

### Generate from another WAV directory

```bash
./scripts/regenerate-audio-headers.sh --source-dir /path/to/wavs
```

## Notes

- Audio is embedded directly in flash as WAV byte arrays
- There is no SPIFFS or LittleFS upload step
- The regeneration script requires `xxd` to be available on `PATH`

## References

- M5Stack Arduino board setup: https://docs.m5stack.com/en/arduino/arduino_board
- M5Stack Arduino library setup: https://docs.m5stack.com/en/arduino/arduino_library
- M5Stack Atom Echo Arduino guide: https://docs.m5stack.com/en/arduino/m5atomecho/program
