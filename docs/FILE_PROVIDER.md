# File creation ownership in iiAudioGen

Generated float32 samples retain their existing WAV serialization. WAV files are staged and published only after encoding succeeds; invalid samples retain an existing destination.

The common [ownership contract](../../iiFileProvider/docs/FILE_OWNERSHIP.md) defines stream, staging, control-file and
installed-package rules. Source regression tests remain with this module; the
workspace creation audit also checks these boundaries. Build/deployment and target
runtime validation are separate evidence.

Changed boundaries:


- `tools/Wav.h`
