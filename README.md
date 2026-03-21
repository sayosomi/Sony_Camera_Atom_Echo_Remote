# AtomEcho ZV-E10 Remote

This sketch turns an M5Stack Atom Echo into a Sony ZV-E10 Bluetooth shutter remote with spoken-style WAV cues through the built-in speaker.

## Button Mapping

- Connected, press `BtnA`: stop any current cue and send focus-half-press immediately.
- Connected, release `BtnA`: start one of `shutter.wav`, `shutter2.wav`, or `shutter3.wav` at random without repeating the previous cue, then fire the shutter immediately.
- Not connected, short press `BtnA`: play `pairing.wav`.
- Not connected, hold `BtnA` for 2 seconds: clear the saved BLE bond and restart pairing.

## Pairing Cue

- On each boot, the first successful camera connection plays `paired.wav` once.
- The paired cue does not replay on later reconnects in the same boot.

## Build

```bash
arduino-cli compile --fqbn m5stack:esp32:m5stack_atom:PartitionScheme=huge_app /Users/yosomi/Code/AtomEcho_ZV-E10_Remote
```

## Upload

Replace the port with the current Atom Echo serial device on this Mac.

```bash
arduino-cli upload -p /dev/cu.usbserial-0001 --fqbn m5stack:esp32:m5stack_atom:PartitionScheme=huge_app /Users/yosomi/Code/AtomEcho_ZV-E10_Remote
```

## Notes

- Audio is embedded in flash as WAV byte arrays; no SPIFFS or LittleFS upload step is required.
- `M5Unified` handles the Atom Echo built-in speaker pinout, so the sketch only needs to start the speaker and queue WAV playback.
