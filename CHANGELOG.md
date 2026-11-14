# Changelog

## 0.2.0

- Added the suite-wide skin system. The free Classic theme remains the default,
  and the header swatch cycles through any installed skins.
- Skin selection is user-level, shared across the suite, and persists in
  `~/.config/hearth/skins.conf`. It never changes audio, presets, or project
  state.
- Added the loader for Catppuccin Mocha, Studio 4000, Tokyo Night, and
  user-created skins.

## 0.1.1

- Fixed the IN/BYPASS button position so it stays inside the UI canvas.

## 0.1.0

- Initial release.
- Transparent lookahead peak limiter with a fixed 5 ms latency window.
- Optional 4x true-peak detection and RMS mode with ceiling safety.
- Input drive, ceiling, release, lookahead, and stereo link controls.
- Input peak, output peak, true peak, RMS, and gain-reduction metering.
- Ardour/Harrison inline-display GR bar for the LV2 build.
- CLAP build with `clap.gain-adjustment-metering` for REAPER track-meter GR.
- Six presets from transparent master to Brick Wall.
- Same native X11/Cairo chassis and controls as the rest of the Hearth series.
