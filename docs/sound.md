<p align="center"><b>English</b> · <a href="sound.ko.md">한국어</a></p>

# Sound

The decomp's runtime renders the DS's sound itself: the game's sequencer (`runtime/sequencer.c`)
and the 16 channels' mixer (`runtime/ds_audio.c`). The port uses both, and adds what a phone
needs and two things of the DS's that Wild World's sound turns out to depend on.

## The game's surround, which needs the DS's way of reading

Wild World does not play its channels straight to the speakers. It has the DS capture the mix
into memory, a thread of the game's changes what was captured (the SDK's "output effect":
surround by default), and channels 1 and 3 play that memory. Capture and playback go round
the same 1024 samples at the same pace.

A DS channel does not read a sample at the moment it plays it. It holds up to 32 bytes that it
read earlier, 16 at a time. So channel 1 reads each place of the round shortly *before* the
capture writes it again: it plays what was captured one round (31 ms) earlier, which the
game's thread has changed by then. The decomp's mixer reads at the moment of playing (its
`docs/sound.md` lists the prefetch among what it does not model), each place has then just
been captured, and the game's effect is never heard: the plain mix comes out, flatter than a
DS's.

`port/src/ds_audio_port.c` is the decomp's mixer with that fetching put around it. Checked
with the title recording: the output is, sample for sample, the old output with the SDK's
surround applied (left plus right unchanged; left minus right `d[i] + 2 d[i-2]`), 1024 samples
later.

## The DS's output rate

The DS puts out a sample every 1024 cycles of its bus clock, 32728.5 a second, which is also
the pace of channels 1 and 3. The decomp renders 32768 a second, and there one sample in 830
of the played-back mix is heard twice. The same file gives the front end's mixer the DS's
grid.

## To the device

`port/src/sound_sdl.c` (the decomp's `runtime/sound.c` with an SDL device in place of
Windows's waveOut):

- the device is asked for 48000 Hz and may answer with its own rate; the port converts with a
  windowed sinc of its own. 32728.5 Hz is not a rate audio hardware has;
- the device's clock and the emulation's are two clocks. A ring of about 80 ms is kept level
  by playing up to half a percent faster or slower;
- when the emulation cannot keep up and the ring runs empty, the sound fades out and in
  instead of clicking; when it runs over, the oldest sound is skipped.

The settings screen on Android shows the device, its rate and how often the ring ran empty
("gaps") or over ("skips"), next to the game's speed. On the desktop the same is in the log
every five seconds, and `--audio-wav FILE` writes the mixer's output to a file.

## Checks

The headless test program (`acww-run`) keeps the decomp's mixer as it is, and its WAV files
are the decomp's. With sound on, the runtime also runs the game's sequencer, and the frames
then differ from the decomp's sound-off recordings; the frame checks are made with sound off.

## To do

A volume setting. The capture unit's own 16-byte delay in writing is not modelled (the order
of reads and writes does not depend on it).
