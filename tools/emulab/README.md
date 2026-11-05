# emulab: try EUC+ in a browser, everything in one container

digikit's Digitakt II emulator runs the real OS 1.17 with EUC+, inside a Docker container. A web
panel in your browser shows the screen and the LEDs, takes the keys and encoders, and plays the
sound of the emulated DSP. Nothing is installed on your Mac but Docker, one image and one volume, and
three commands remove all of it.

**What it is good for:** seeing and hearing EUC+ before the hardware test, trying the pages, and
checking that the notes change. **What it is not:** a real unit. The DSP is emulated in software, so
emulated time, and the sound, run slower than real time; the panel shows the factor (measured in a
4-CPU cloud machine: about 7 times slower with the real DSP, about 2 times slower in the fast mode;
digikit reports near real time on a desktop). Timing, feel and the sound of the real converters
still need the hardware test.

**Status (2026-11-05):** the panel and each setup step were run in a Linux cloud session; the image
builds. A full first run of the container from scratch has not been done yet, and it has not been
run on a Mac.

## What you need

- **Docker on your Mac**: [Docker Desktop](https://www.docker.com/products/docker-desktop/), or
  [OrbStack](https://orbstack.dev) or [colima](https://github.com/abiosoft/colima), any one of them.
  In Docker Desktop's settings, give it at least **4 CPUs and 8 GB of memory** (the first build
  compiles the emulator).
- About **15 GB of free disk** for the volume.
- An internet connection on the first run: it downloads the stock OS 1.17 from elektron.se (checked
  against its SHA-256), Elekloader, digikit and their dependencies, into the volume.

## Run it

From this repository's folder in Terminal:

```sh
docker build -t digitorsion-emulab tools/emulab
docker run --rm -it --name emulab -p 127.0.0.1:8080:8080 \
    -v digitorsion-work:/work -v "$PWD":/repo:ro \
    digitorsion-emulab
```

Then open <http://localhost:8080>.

- **The first run sets everything up**, about 30 to 60 minutes depending on your Mac:
  - the OS, the emulator and its starting state;
  - a fast DSP core made for this OS;
  - EUC+ built from this repository.

  Later runs take a minute or two: they rebuild EUC+ from the repository as it is, then boot.
- **Stop it** with Ctrl-C in Terminal.
- **Your own samples:** add `-v /path/to/a/folder/of/wavs:/samples:ro` to `docker run`. They go on the
  emulated +Drive; without it, a plucked test tone is there.
- **A faster mode, without sound:** add `fast` at the end of `docker run`. A silent stand-in DSP keeps
  the sequencer running, several times faster than with the real DSP. The panel then shows the notes EUC+ chooses and the
  MIDI notes sent, but plays nothing.

## Using the panel

- **Keys:** click them. FUNC, TRK and PAGE **latch**: click once to hold, again to release. So FUNC +
  AMP is: click FUNC, click AMP, click FUNC. On the keyboard, Shift holds FUNC.
- **Encoders:** drag up or down, or scroll. Shift + scroll sends ten ticks; − / + send five. As on the
  unit, stock parameters need several ticks per step, and switches such as EUC about a quarter turn.
  Click a knob to push it.
- **Sound:** click **Start sound** (browsers need a click first). Because the emulator is slower than
  real time, the live sound has gaps. **Record**, then stop, gives a take you can play back at the
  right speed, or download as a WAV.
- **EUC+ notes:** the panel lists the last notes EUC+ chose (track, step, stock note → EUC+ note), for
  audio and MIDI tracks.

To try EUC+, follow the hardware test list in [PLAN.md](../../PLAN.md) ("Hardware builds", E201).

## Remove everything

```sh
docker volume rm digitorsion-work      # the OS, the emulator, its builds and state (about 15 GB)
docker rmi digitorsion-emulab          # the image
docker builder prune                   # Docker's build cache
```

Then uninstall Docker itself if you installed it only for this. Nothing else was written to your Mac.

## Notes

- The volume holds Elektron's OS and files made from it (the emulator's state, the built OS with
  EUC+). It is yours, on your Mac; do not share its contents.
- Your repository is mounted read-only. Each run copies it into the volume and builds from the copy,
  so the container never writes to your files.
- The port is bound to `127.0.0.1`: only your Mac can open the panel.
