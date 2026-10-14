libass [![Coverity scan build status](https://scan.coverity.com/projects/3531/badge.svg)](https://scan.coverity.com/projects/3531) [![Build status](https://github.com/libass/libass/actions/workflows/ghci.yml/badge.svg?branch=master&event=push)](https://github.com/libass/libass/actions/workflows/ghci.yml?query=branch%3Amaster+event%3Apush)
======
libass is a portable subtitle renderer for the ASS/SSA (Advanced Substation Alpha/Substation Alpha) subtitle format. It is mostly compatible with VSFilter.

About this fork
===============
This is [altqx/libass](https://github.com/altqx/libass), a fork of upstream libass that feeds native and WebAssembly consumers (e.g. [AkariSub](https://github.com/altqx/akarisub) and HikariSub). It tracks upstream `master` and adds performance work and a few fork-only APIs. **Rendering output is bit-identical to upstream**: every change is checked frame by frame against upstream on x86-64, arm64 and wasm32.

Performance against upstream `60679ac`. Times are the minimum render-loop thread CPU time over 9-56 interleaved runs, pinned to one core. Machine: x86-64, Ryzen 5 5600, gcc 16.2, `-O3`:

| Workload | upstream | fork | change |
|---|---:|---:|---:|
| Full anime episode, heavy typesetting, 34,692 frames at 1080p | 9.33 s | 4.50 s | 2.07x faster |
| Same episode, 3,000 sampled frames at 720p | 854 ms | 554 ms | 1.54x faster |
| Opening sequence, 192 frames | 39.1 ms | 22.5 ms | 1.74x faster |
| Synthetic stress script, 2,000 sampled frames | 4.66 s | 2.78 s | 1.68x faster |
| Synthetic stress script, 1,464 consecutive frames | 965 ms | 786 ms | 1.23x faster |
| libass `compare` tests | 52.2 / 2.31 ms | 51.9 / 2.30 ms | neutral |
| Bytes allocated while rendering the full episode | 7.54 GB | 3.38 GB | -55% |
| `ass_renderer_init` + `ass_renderer_done` | 1.17 ms | 8.3 µs | about 140x faster |
| `ass_set_frame_size` with empty caches | 666 µs | 0.03 µs | effectively free |

The main changes are listed below; each commit message has its details and measurements:

- **Event selection:** per-64-event block summaries let `ass_render_frame` skip events that cannot be active, so per-frame cost no longer grows with the length of the track.
- **Override tags:** compiled tags are cached by block content (a fixed 4 MiB cache), and tag names are matched only after their first character.
- **Allocations:** image nodes and composite bitmap runs are reused across frames, the blur scratch buffer is reused (capped at 4 MiB), blur parameter search is memoized, the FriBidi joining-type buffer is kept per shaper, and collision placement inserts into a sorted list instead of re-sorting it.
- **Caches:** hash tables start small and grow on demand (about 14 KiB of buckets instead of 3 MiB), and a use-after-free in `ass_cache_empty` is fixed.
- **Bitmap kernels:** `fix_outline` and `shift_bitmap` have SSE2/AVX2/NEON/WASM SIMD kernels.
- **wasm32:** horizontal blur taps stay in registers, and kernels are called directly instead of through `call_indirect`.

Fork-only API (declared in `libass/ass.h`):

- `ass_track_events_modified(track, first_eid)`, macro `LIBASS_HAS_TRACK_EVENTS_MODIFIED`. Events changed through the libass API, or appended by hand, are tracked automatically. **If your application writes `Start`/`Duration` of existing events, or inserts or reorders events after rendering has started, call this before the next `ass_render_frame`, or the changed lines may not be shown.** A track must not be rendered by several threads at once until it has been rendered once after a change.
- `ass_frame_pack_atlas(renderer, images, max_size, padding, max_pages)` and `ass_frame_atlas_reset(renderer)`, macro `LIBASS_HAS_FRAME_ATLAS`. They pack a frame's bitmaps into R8 texture pages plus a 32-byte instance per image, so a GPU renderer can draw a frame with one upload and one instanced draw. Unchanged pixels and instances are reported, so they can be skipped.
- `ass_set_glyph_run_callback` is now exported from shared builds.

`LIBASS_VERSION` is `0x01705010`. Upstream uses the same low-byte steps for its own API additions between releases, so detect fork features with the `LIBASS_HAS_*` macros, not with the version number.

Get it
======
See [GitHub releases](https://github.com/libass/libass/releases) for the latest release 0.17.5 (released 2026-06-24).
See the [changelog](https://github.com/libass/libass/blob/master/Changelog) for a detailed list of changes.

Source code is available from our [GitHub repository](https://github.com/libass/libass).

Contact
=======
Please use the [issue tracker](https://github.com/libass/libass/issues?state=open) to report bugs or feature requests.

We have an IRC channel, too. Talk to us on [irc.libera.chat/#libass](https://web.libera.chat/#libass). Note that we cannot be online all the time and we cannot answer IRC questions if you leave the channel. Even if you do not get an immediate response, keep your IRC client open, and we will eventually get back to you.

Building
========

libass offers two build systems to choose from: Autotools and Meson.

Autotools is preferred for development since it integrates with our testing
infrastructure and is feature-complete on all platforms supported by Autotools.  
If you are packaging libass for distribution, Autotools is recommended;
when packaging for Windows Meson should work equally well.

Meson lacks integration with testing infrastructure, but works otherwise well on
Windows. It is suited for static-only builds on any platform well supported by
Meson and as a Meson subproject.
Notably, Meson supports MSVC and generation of VS project files.

Macro defines
-------------

Unless developing libass there’s usually no need to manually define macros
and there are no stability guarantees for these manual defines.

- `DEBUG_LEVEL=0..3`
  - `0` use the default set of asserts; implied if macro is not defined at all
  - `1`, `2` *unused*
  - `3` additionally assert assumptions usually pledged to the compiler for optimization purposes

Information about the ASS format:
=================================
- [ASS format overview](https://github.com/libass/libass/wiki/ASS-File-Format-Guide)
- [ASS override tags (Aegisub manual)](http://docs.aegisub.org/latest/ASS_Tags/)
- [VSFilter source code (Guliverkli2)](http://sourceforge.net/p/guliverkli2/code/HEAD/tree/src/subtitles/)

Other ASS/SSA implementations:
==============================
- VSFilter:
  - [xy-VSFilter/XySubFilter](https://github.com/Cyberbeing/xy-VSFilter/)
    - [pfmod](https://github.com/pinterf/xy-VSFilter/)
  - VSFilter in [MPC-HC](https://github.com/clsid2/mpc-hc/tree/develop/src/filters/transform/VSFilter/)
  - [VSFilterMod](https://code.google.com/archive/p/vsfiltermod/) with custom format extensions (defunct, subsumed by forks)
    - [sorayuki fork](https://github.com/sorayuki/VSFilterMod/) with some bugfixes (defunct)
    - various forks focussing on internal use
  - [Threaded VSFilter](https://code.google.com/p/threaded-vsfilter/) (defunct)
  - VSFilter in [Guliverkli2](http://sourceforge.net/projects/guliverkli2/) (defunct, subsumed by all of the above)
  - VSFilter in [guliverkli](http://sourceforge.net/projects/guliverkli/) (defunct, forked as Guliverkli2)
- [ffdshow](http://ffdshow-tryout.sourceforge.net/) (defunct)
- [Perian](https://github.com/MaddTheSane/perian) (defunct)
- [asa](https://web.archive.org/web/20110906033709/http://asa.diac24.net/) (defunct)
- [libjass](https://github.com/Arnavion/libjass) (defunct)
- [ASS.js](https://github.com/weizhenye/ASS)

Packaging status
================

[![Packaging status](https://repology.org/badge/vertical-allrepos/libass.svg?columns=3&header=libass&exclude_unsupported=1)](https://repology.org/project/libass/versions)
