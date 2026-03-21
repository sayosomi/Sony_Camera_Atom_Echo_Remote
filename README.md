# AtomEcho ZV-E10 Remote

This sketch turns an M5Stack Atom Echo into a Sony ZV-E10 Bluetooth shutter remote with spoken-style WAV cues through the built-in speaker.

## Button Mapping

- Connected, press `BtnA`: stop any current cue and send focus-half-press immediately.
- Connected, release `BtnA`: start one of `shutter0.wav` through `shutter4.wav` at random, never repeat the previous shutter cue, and exclude `shutter0.wav` from the first shot after boot.
- Not connected, short press `BtnA`: play `pairing.wav` when no saved camera exists, otherwise play `connecting.wav`.
- Not connected, hold `BtnA` for 2 seconds: clear the saved BLE bond and restart pairing.

## Connection Cues

- On boot with no saved camera, play `pairing.wav` once.
- On boot with a saved camera, play `connecting.wav` once.
- On any successful camera connection, play `connected.wav`.
- Automatic reconnect retries do not replay the scan cue.
- After clearing bond data with a long press, play `pairing.wav`.

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
