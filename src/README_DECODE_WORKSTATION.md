# RetroSpectrum Decode Workstation

This update adds a new `DECODE` dashboard tab that reads `.complex16` recordings from the same `-o` recording directory used by RetroSpectrum.

## New files

- `DecodeWorkstation.c`
- `DecodeWorkstation.h`

## Modified files

- `RetroSpectrum.c`
- `MapDashboard.c`
- `MapDashboard.h`

## Dependencies

Install liquid-dsp development headers/libraries:

```bash
sudo apt install liquid-dsp libliquid-dev
```

Add the new C file to your build and link liquid-dsp:

```bash
gcc ... src/DecodeWorkstation.c ... -lliquid -lm
```

If your project uses a Makefile, add `src/DecodeWorkstation.c` to the source list and add `-lliquid` to the link libraries.

## Controls

- Top tab: `DECODE`
- Keyboard: `5` or `F5` opens Decode; Case Management moved to `6` / `F6`
- `Search`: popup filename search for `.complex16`, `.c16`, or `.iq16`
- `Rescan`: refresh recording list
- `Decode`: run liquid-dsp demodulation on the selected file
- `Enter`: decode when no text field is active
- `R`: rescan files
- `C`: clear bitstream
- `Ctrl+D`: return to dashboard

## Supported liquid-dsp modulations

- OOK / ASK: `LIQUID_MODEM_OOK`
- BPSK: `LIQUID_MODEM_BPSK`
- QPSK: `LIQUID_MODEM_QPSK`

## Decode controls

- `Samples/Symbol`: how many complex16 samples are averaged into one symbol decision
- `Bits/Symbol`: how many bits are printed per liquid symbol decision; defaults to 1 for OOK/BPSK and 2 for QPSK
- `Start Sample`: skips this many complex samples before decoding
- `Max Symbols`: maximum symbol decisions to read from the file
- `Normalize`: scales sampled symbols to unit average power before demodulating
- `Invert bits`: flips output bits
- `Tight stream`: removes spaces between symbol decisions

This is symbol-level demodulation. It assumes the file is already centered well enough and that `Samples/Symbol` approximately matches the signal's symbol timing. Use Analysis first to estimate symbol spacing.
