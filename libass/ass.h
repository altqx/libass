/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
 * Copyright (C) 2011 Grigori Goronzy <greg@chown.ath.cx>
 *
 * This file is part of libass.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef LIBASS_ASS_H
#define LIBASS_ASS_H

#include <stdio.h>
#include <stdarg.h>
#include "ass_types.h"

#define LIBASS_VERSION 0x01705040

#ifdef __cplusplus
extern "C" {
#endif

#if (defined(__GNUC__) && ((__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))) || defined(__clang__)
    #if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5) || defined(__clang__)
        #define ASS_DEPRECATED(msg) __attribute__((deprecated(msg)))
    #else
        #define ASS_DEPRECATED(msg) __attribute__((deprecated))
    #endif
    #if __GNUC__ > 5 || defined(__clang__)
        #define ASS_DEPRECATED_ENUM(msg) __attribute__((deprecated(msg)))
    #else
        #define ASS_DEPRECATED_ENUM(msg)
    #endif
#elif defined(_MSC_VER)
    #define ASS_DEPRECATED(msg) __declspec(deprecated(msg))
    #define ASS_DEPRECATED_ENUM(msg)
#else
    #define ASS_DEPRECATED(msg)
    #define ASS_DEPRECATED_ENUM(msg)
#endif


/*
 * A linked list of images produced by an ass renderer.
 *
 * These images have to be rendered in-order for the correct screen
 * composition.  The libass renderer clips these bitmaps to the frame size.
 * w/h can be zero, in this case the bitmap should not be rendered at all.
 * The last bitmap row is not guaranteed to be padded up to stride size,
 * e.g. in the worst case a bitmap has the size stride * (h - 1) + w.
 */
typedef struct ass_image {
    int w, h;                   // Bitmap width/height
    int stride;                 // Bitmap stride
    unsigned char *bitmap;      // 1-byte-per-pixel alpha buffer
                                // Note: the last row may not be padded to
                                // bitmap stride!
                                // The guaranteed allocated size is
                                // `(stride * (h - 1)) + w`, and bytes past
                                // the width in each line may be uninitialized.
    uint32_t color;             // Bitmap color and alpha, RGBA
                                // For full VSFilter compatibility, the value
                                // must be transformed as described in
                                // ass_types.h for ASS_YCbCrMatrix
    int dst_x, dst_y;           // Bitmap placement inside the video frame

    struct ass_image *next;   // Next image, or NULL

    enum {
        IMAGE_TYPE_CHARACTER,
        IMAGE_TYPE_OUTLINE,
        IMAGE_TYPE_SHADOW
    } type;

    // New fields can be added here in new ABI-compatible library releases.
} ASS_Image;

/*
 * Hinting type. (see ass_set_hinting below)
 *
 * Setting hinting to anything but ASS_HINTING_NONE will put libass in a mode
 * that reduces compatibility with vsfilter and many ASS scripts. The main
 * problem is that hinting conflicts with smooth scaling, which precludes
 * animations and precise positioning.
 *
 * In other words, enabling hinting might break some scripts severely.
 *
 * FreeType's native hinter is still buggy sometimes and it is recommended
 * to use the light autohinter, ASS_HINTING_LIGHT, instead.  For best
 * compatibility with problematic fonts, disable hinting.
 */
typedef enum {
    ASS_HINTING_NONE = 0,
    ASS_HINTING_LIGHT,
    ASS_HINTING_NORMAL,
    ASS_HINTING_NATIVE
} ASS_Hinting;

/**
 * \brief Text shaping levels.
 *
 * SIMPLE is a fast, font-agnostic shaper that can do only substitutions.
 * COMPLEX is a slower shaper using OpenType for substitutions and positioning.
 *
 * libass uses the best shaper available by default.
 */
typedef enum {
    ASS_SHAPING_SIMPLE = 0,
    ASS_SHAPING_COMPLEX
} ASS_ShapingLevel;

/**
 * \brief Style override options. See
 * ass_set_selective_style_override_enabled() for details.
 */
typedef enum {
    /**
     * Default mode (with no other bits set). All selective override features
     * as well as the style set with ass_set_selective_style_override() are
     * disabled, but traditional overrides like ass_set_font_scale() are
     * applied unconditionally.
     */
    ASS_OVERRIDE_DEFAULT = 0,
    /**
     * Apply the style as set with ass_set_selective_style_override() on events
     * which look like dialogue. Other style overrides are also applied this
     * way, except ass_set_font_scale(). How ass_set_font_scale() is applied
     * depends on the ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE flag.
     *
     * This is equivalent to setting all of the following bits:
     *
     * ASS_OVERRIDE_BIT_FONT_NAME
     * ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS
     * ASS_OVERRIDE_BIT_COLORS
     * ASS_OVERRIDE_BIT_BORDER
     * ASS_OVERRIDE_BIT_ATTRIBUTES
     */
    ASS_OVERRIDE_BIT_STYLE = 1 << 0,
    /**
     * Apply ass_set_font_scale() only on events which look like dialogue.
     * If not set, the font scale is applied to all events. (The behavior and
     * name of this flag are unintuitive, but exist for compatibility)
     */
    ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE = 1 << 1,
    /**
     * Old alias for ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE. Deprecated. Do not use.
     */
    ASS_OVERRIDE_BIT_FONT_SIZE ASS_DEPRECATED_ENUM("replaced by ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE") = 1 << 1,
    /**
     * On dialogue events override: FontSize, Spacing, ScaleX, ScaleY
     */
    ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS = 1 << 2,
    /**
     * On dialogue events override: FontName, treat_fontname_as_pattern
     */
    ASS_OVERRIDE_BIT_FONT_NAME = 1 << 3,
    /**
     * On dialogue events override: PrimaryColour, SecondaryColour, OutlineColour, BackColour
     */
    ASS_OVERRIDE_BIT_COLORS = 1 << 4,
    /**
     * On dialogue events override: Bold, Italic, Underline, StrikeOut
     */
    ASS_OVERRIDE_BIT_ATTRIBUTES = 1 << 5,
    /**
     * On dialogue events override: BorderStyle, Outline, Shadow
     */
    ASS_OVERRIDE_BIT_BORDER = 1 << 6,
    /**
     * On dialogue events override: Alignment
     */
    ASS_OVERRIDE_BIT_ALIGNMENT = 1 << 7,
    /**
     * On dialogue events override: MarginL, MarginR, MarginV
     */
    ASS_OVERRIDE_BIT_MARGINS = 1 << 8,
    /**
     * Unconditionally replace all fields of all styles with the one provided
     * with ass_set_selective_style_override().
     * Does not apply ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE.
     * Add ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS and ASS_OVERRIDE_BIT_BORDER if
     * you want FontSize, Spacing, Outline, Shadow to be scaled to the script
     * resolution given by the ASS_Track.
     */
    ASS_OVERRIDE_FULL_STYLE = 1 << 9,
    /**
     * On dialogue events override: Justify
     */
    ASS_OVERRIDE_BIT_JUSTIFY = 1 << 10,
    /**
     * On dialogue events override: Blur
     */
    ASS_OVERRIDE_BIT_BLUR = 1 << 11,
    // New enum values can be added here in new ABI-compatible library releases.
} ASS_OverrideBits;

/**
 * \brief Return the version of library. This returns the value LIBASS_VERSION
 * was set to when the library was compiled.
 * \return library version
 */
int ass_library_version(void);

/**
 * \brief Default Font provider to load fonts in libass' database
 *
 * NONE don't use any default font provider for font lookup
 * AUTODETECT use the first available font provider
 * CORETEXT force a CoreText based font provider (OS X only)
 * DIRECTWRITE force a DirectWrite based font provider (Microsoft Win32 only)
 * FONTCONFIG force a Fontconfig based font provider
 *
 * libass uses the best shaper available by default.
 */
typedef enum {
    ASS_FONTPROVIDER_NONE       = 0,
    ASS_FONTPROVIDER_AUTODETECT = 1,
    ASS_FONTPROVIDER_CORETEXT,
    ASS_FONTPROVIDER_FONTCONFIG,
    ASS_FONTPROVIDER_DIRECTWRITE,
} ASS_DefaultFontProvider;

typedef enum {
    /**
     * Enable libass extensions that would display ASS subtitles incorrectly.
     * These may be useful for applications, which use libass as renderer for
     * subtitles converted from another format, or which use libass for other
     * purposes that do not involve actual ASS subtitles authored for
     * distribution.
     */
    ASS_FEATURE_INCOMPATIBLE_EXTENSIONS,

    /**
     * Match bracket pairs in bidirectional text according to the revised
     * Unicode Bidirectional Algorithm introduced in Unicode 6.3.
     * This is incompatible with VSFilter and disabled by default.
     *
     * (Directional isolates, also introduced in Unicode 6.3,
     * are unconditionally processed when FriBidi is new enough.)
     *
     * This feature may be unavailable at runtime (ass_track_set_feature
     * may return -1) if libass was compiled against old FriBidi.
     */
    ASS_FEATURE_BIDI_BRACKETS,

    /**
     * When this feature is disabled, text is split into VSFilter-compatible
     * segments and text in each segment is processed in isolation.
     * Notably, this includes running the Unicode Bidirectional
     * Algorithm and shaping the text within each run separately.
     * The individual runs are then laid out left-to-right,
     * even if they contain right-to-left text.
     *
     * When this feature is enabled, each event's text is processed as a whole
     * (as far as possible). In particular, the Unicode Bidirectional
     * Algorithm is run on the whole text, and text is shaped across
     * override tags.
     *
     * This is incompatible with VSFilter and disabled by default.
     *
     * libass extensions to ASS such as Encoding -1 can cause individual
     * events to be always processed as if this feature is enabled.
     */
    ASS_FEATURE_WHOLE_TEXT_LAYOUT,

    /**
     * Break lines according to the Unicode Line Breaking Algorithm.
     * If the track language is set, some additional language-specific tweaks
     * may be applied. Setting this enables more breaking opportunities
     * compared to classic ASS. However, it is still possible for long words
     * without breaking opportunities to cause overfull lines.
     * This is incompatible with VSFilter and disabled by default.
     *
     * This feature may be unavailable at runtime if
     * libass was compiled without libunibreak support.
     */
    ASS_FEATURE_WRAP_UNICODE,

    // New enum values can be added here in new ABI-compatible library releases.
} ASS_Feature;

/**
 * \brief Initialize the library.
 * \return library handle or NULL if failed
 */
ASS_Library *ass_library_init(void);

/**
 * \brief Finalize the library
 * \param priv library handle
 */
void ass_library_done(ASS_Library *priv);

/**
 * \brief Set additional fonts directory.
 * Optional directory that will be scanned for fonts.  The fonts
 * found are used for font lookup.
 * NOTE: A valid font directory is not needed to support embedded fonts.
 * On Microsoft Windows, when using WIN32-APIs, fonts_dir must be in either
 * UTF-8 mixed with lone or paired UTF-16 surrogates encoded like in CESU-8
 * or the encoding accepted by fopen with the former taking precedence
 * if both versions are valid and exist.
 * On all other systems there is no need for special considerations like that.
 *
 * \param priv library handle
 * \param fonts_dir directory with additional fonts
 */
void ass_set_fonts_dir(ASS_Library *priv, const char *fonts_dir);

/**
 * \brief Whether fonts should be extracted from track data.
 * \param priv library handle
 * \param extract whether to extract fonts
 */
void ass_set_extract_fonts(ASS_Library *priv, int extract);

/**
 * \brief Register style overrides with a library instance.
 * The overrides should have the form [Style.]Param=Value, e.g.
 *   SomeStyle.Font=Arial
 *   ScaledBorderAndShadow=yes
 *
 * \param priv library handle
 * \param list NULL-terminated list of strings,
 *             copied and never modified by libass
 */
#ifdef __cplusplus
void ass_set_style_overrides(ASS_Library *priv, const char *const *list);
#else
void ass_set_style_overrides(ASS_Library *priv, char **list);
#endif

/**
 * \brief Explicitly process style overrides for a track.
 * \param track track handle
 */
void ass_process_force_style(ASS_Track *track);

/**
 * \brief Register a callback for debug/info messages.
 * If a callback is registered, it is called for every message emitted by
 * libass.  The callback receives a format string and a list of arguments,
 * to be used for the printf family of functions. Additionally, a log level
 * from 0 (FATAL errors) to 7 (verbose DEBUG) is passed.  Usually, level 5
 * should be used by applications.
 * If no callback is set, all messages level < 5 are printed to stderr,
 * prefixed with [ass].
 *
 * \param priv library handle
 * \param msg_cb pointer to callback function
 * \param data additional data, will be passed to callback
 */
void ass_set_message_cb(ASS_Library *priv, void (*msg_cb)
                        (int level, const char *fmt, va_list args, void *data),
                        void *data);

/**
 * Font diagnostics (fork extension, not available in upstream libass; its
 * presence is indicated by LIBASS_HAS_FONT_DIAGNOSTICS).
 *
 * Reports which font files, faces and fallbacks the renderer actually uses,
 * so that an application can verify font identity (for example, to collect
 * the fonts a script needs) instead of trusting a name lookup. The hooks only
 * observe; rendering is unchanged. The callbacks run during rendering, on the
 * thread that renders; strings, the coords array and the read function are
 * valid only for the duration of the call.
 * Fonts and glyphs are cached by the renderer, so the callbacks report cache
 * misses: a font is reported when a renderer first loads it. To observe
 * everything a script uses, render it with a fresh renderer.
 */
#define LIBASS_HAS_FONT_DIAGNOSTICS 1
#define ASS_FONT_DIAGNOSTICS_VERSION 1

/**
 * \brief How a font was selected.
 */
typedef enum {
    ASS_FONT_STAGE_REQUESTED = 1,  // the requested family (or a provider substitution)
    ASS_FONT_STAGE_DEFAULT_FAMILY, // the configured default family
    ASS_FONT_STAGE_FALLBACK,       // the font provider's glyph fallback
    ASS_FONT_STAGE_DEFAULT_PATH,   // the configured default font file
} ASS_FontStage;

/**
 * \brief A font chosen by the font selector.
 */
typedef struct ass_font_selection {
    ASS_FontStage stage;
    const char *requested_family;   // may be NULL or empty
    unsigned bold, italic;
    uint32_t code;                  // the character that needed a face (0: none)
    const char *path;               // NULL when the bytes come from a stream
    const char *display_name;
    const char *postscript_name;    // may be NULL
    int index;                      // face index requested from the file or stream
    int uid;                        // selector-local face id; -1 for the default path
    int embedded;                   // memory font (ass_add_font or extracted attachment)
    // Reads stream bytes; with data NULL, returns the stream size. NULL for files.
    size_t (*read)(void *read_data, unsigned char *data, size_t offset, size_t len);
    void *read_data;
} ASS_FontSelection;

/**
 * \brief Font diagnostics callbacks. Any callback may be NULL.
 */
typedef struct ass_font_diagnostics {
    int version; // must be ASS_FONT_DIAGNOSTICS_VERSION
    // When the font selector chose a font for a family or a character.
    void (*selected)(void *data, const ASS_FontSelection *selection);
    // After FreeType opened a selection: the actual face index (named-instance
    // bits included), the file's face count and the design coordinates, if any
    // (coordinates need FreeType 2.9 or newer; n_coords is 0 otherwise).
    void (*face_opened)(void *data, int uid, long face_index, long num_faces,
                        const char *postscript_name, int n_coords, const double *coords);
    // When a glyph is synthetically emboldened or slanted.
    void (*glyph_simulated)(void *data, int uid, unsigned glyph_index,
                            int embolden, int italicize);
    // When no available face has the character.
    void (*glyph_missing)(void *data, uint32_t code, const char *family,
                          unsigned bold, unsigned italic);
} ASS_FontDiagnostics;

/**
 * \brief Install font diagnostics callbacks.
 * \param priv library handle
 * \param callbacks callbacks to install; the struct is copied. NULL, or a
 * version other than ASS_FONT_DIAGNOSTICS_VERSION, removes them.
 * \param data additional data, will be passed to the callbacks
 */
void ass_set_font_diagnostics(ASS_Library *priv,
                              const ASS_FontDiagnostics *callbacks,
                              void *data);

/**
 * \brief Initialize the renderer.
 * \param priv library handle
 * \return renderer handle or NULL if failed
 *
 * NOTE: before rendering starts the renderer should also be
 *       configured with at least ass_set_storage_size(),
 *       ass_set_frame_size() and ass_set_fonts();
 *       see respective docs.
 */
ASS_Renderer *ass_renderer_init(ASS_Library *);

/**
 * \brief Finalize the renderer.
 * \param priv renderer handle
 */
void ass_renderer_done(ASS_Renderer *priv);

/**
 * \brief Set the frame size in pixels, including margins.
 * The renderer will never return images that are outside of the frame area.
 * The value set with this function can influence the pixel aspect ratio used
 * for rendering.
 * If after compensating for configured margins the frame size
 * is not an isotropically scaled version of the video display size,
 * you may have to use ass_set_pixel_aspect().
 * @see ass_set_pixel_aspect()
 * @see ass_set_margins()
 * \param priv renderer handle
 * \param w width
 * \param h height
 *
 * NOTE: frame size must be configured before an ASS_Renderer can be used.
 */
void ass_set_frame_size(ASS_Renderer *priv, int w, int h);

/**
 * \brief Set the source image size in pixels.
 * This affects some ASS tags like e.g. 3D transforms and
 * is used to calculate the source aspect ratio and blur scale.
 * If subtitles specify valid LayoutRes* headers, those will take precedence.
 * The source image size can be reset to default by setting w and h to 0.
 * The value set with this function can influence the pixel aspect ratio used
 * for rendering.
 * The values must be the actual storage size of the video stream,
 * without any anamorphic de-squeeze applied.
 * @see ass_set_pixel_aspect()
 * \param priv renderer handle
 * \param w width
 * \param h height
 *
 * NOTE: storage size must be configured to get correct results,
 *       otherwise libass is forced to make a fallible guess.
 */
void ass_set_storage_size(ASS_Renderer *priv, int w, int h);

/**
 * \brief Set shaping level. This is merely a hint, the renderer will use
 * whatever is available if the request cannot be fulfilled.
 * \param level shaping level
 */
void ass_set_shaper(ASS_Renderer *priv, ASS_ShapingLevel level);

/**
 * \brief Set frame margins.  These values may be negative if pan-and-scan
 * is used. The margins are in pixels. Each value specifies the distance from
 * the video rectangle to the renderer frame. If a given margin value is
 * positive, there will be free space between renderer frame and video area.
 * If a given margin value is negative, the frame is inside the video, i.e.
 * the video has been cropped.
 *
 * The renderer will try to keep subtitles inside the frame area. If possible,
 * text is layout so that it is inside the cropped area. Subtitle events
 * that can't be moved are cropped against the frame area.
 *
 * ass_set_use_margins() can be used to allow libass to render subtitles into
 * the empty areas if margins are positive, i.e. the video area is smaller than
 * the frame. (Traditionally, this has been used to show subtitles in
 * the bottom "black bar" between video bottom screen border when playing 16:9
 * video on a 4:3 screen.)
 *
 * \param priv renderer handle
 * \param t top margin
 * \param b bottom margin
 * \param l left margin
 * \param r right margin
 */
void ass_set_margins(ASS_Renderer *priv, int t, int b, int l, int r);

/**
 * \brief Whether margins should be used for placing regular events.
 * \param priv renderer handle
 * \param use whether to use the margins
 */
void ass_set_use_margins(ASS_Renderer *priv, int use);

/**
 * \brief Set pixel aspect ratio correction.
 * This is the ratio of pixel width to pixel height.
 *
 * Generally, this is (d_w / d_h) / (s_w / s_h), where s_w and s_h is the
 * video storage size, and d_w and d_h is the video display size. (Display
 * and storage size can be different for anamorphic video, such as DVDs.)
 *
 * If the pixel aspect ratio is 0, or if the aspect ratio has never been set
 * by calling this function, libass will calculate a default pixel aspect ratio
 * out of values set with ass_set_frame_size() and ass_set_storage_size(). Note
 * that this default assumes the frame size after compensating for margins
 * corresponds to an isotropically scaled version of the video display size.
 * If the storage size has not been set, a pixel aspect ratio of 1 is assumed.
 *
 * If subtitles specify valid LayoutRes* headers, the API-configured
 * pixel aspect value is discarded in favour of one calculated out of the
 * headers and values set with ass_set_frame_size().
 *
 * \param priv renderer handle
 * \param par pixel aspect ratio (1.0 means square pixels, 0 means default)
 */
void ass_set_pixel_aspect(ASS_Renderer *priv, double par);

/**
 * \brief Set aspect ratio parameters.
 * This calls ass_set_pixel_aspect(priv, dar / sar).
 * @deprecated New code should use ass_set_pixel_aspect().
 * \param priv renderer handle
 * \param dar display aspect ratio (DAR), prescaled for output PAR
 * \param sar storage aspect ratio (SAR)
 */
ASS_DEPRECATED("use 'ass_set_pixel_aspect' instead")
void ass_set_aspect_ratio(ASS_Renderer *priv, double dar, double sar);

/**
 * \brief Set a fixed font scaling factor.
 * \param priv renderer handle
 * \param font_scale scaling factor, default is 1.0
 */
void ass_set_font_scale(ASS_Renderer *priv, double font_scale);

/**
 * \brief Set font hinting method.
 * \param priv renderer handle
 * \param ht hinting method
 */
void ass_set_hinting(ASS_Renderer *priv, ASS_Hinting ht);

/**
 * \brief Set line spacing. Will not be scaled with frame size.
 * \param priv renderer handle
 * \param line_spacing line spacing in pixels
 */
void ass_set_line_spacing(ASS_Renderer *priv, double line_spacing);

/**
 * \brief Set vertical line position.
 * \param priv renderer handle
 * \param line_position vertical line position of subtitles in percent
 * (0-100: 0 = on the bottom (default), 100 = on top)
 */
void ass_set_line_position(ASS_Renderer *priv, double line_position);

/**
 * \brief Get the list of available font providers. The output array
 * is allocated with malloc and can be released with free(). If an
 * allocation error occurs, size is set to (size_t)-1.
 * \param priv library handle
 * \param providers output, list of default providers (malloc'ed array)
 * \param size output, number of providers
 * \return list of available font providers (user owns the returned array)
 */
void ass_get_available_font_providers(ASS_Library *priv,
                                      ASS_DefaultFontProvider **providers,
                                      size_t *size);

/**
 * \brief Set font lookup defaults.
 * \param default_font path to default font to use. Must be supplied if
 * all system fontproviders are disabled or unavailable.
 * \param default_family fallback font family, or NULL
 * \param dfp which font provider to use (one of ASS_DefaultFontProvider). In
 * older libass version, this could be 0 or 1, where 1 enabled fontconfig.
 * Newer releases also accept 0 (ASS_FONTPROVIDER_NONE) and 1
 * (ASS_FONTPROVIDER_AUTODETECT), which is almost backward-compatible.
 * If the requested fontprovider does not exist or fails to initialize, the
 * behavior is the same as when ASS_FONTPROVIDER_NONE was passed.
 * \param config path to fontconfig configuration file, or NULL.  Only relevant
 * if fontconfig is used. The encoding must match the one accepted by fontconfig.
 * \param update whether fontconfig cache should be built/updated now.  Only
 * relevant if fontconfig is used.
 *
 * NOTE: font lookup must be configured before an ASS_Renderer can be used.
 */
void ass_set_fonts(ASS_Renderer *priv, const char *default_font,
                   const char *default_family, int dfp,
                   const char *config, int update);

/**
 * \brief Set selective style override mode.
 * If enabled, the renderer attempts to override the ASS script's styling of
 * normal subtitles, without affecting explicitly positioned text. If an event
 * looks like a normal subtitle, parts of the font style are copied from the
 * user style set with ass_set_selective_style_override().
 * Warning: the heuristic used for deciding when to override the style is rather
 *          rough, and enabling this option can lead to incorrectly rendered
 *          subtitles. Since the ASS format doesn't have any support for
 *          allowing end-users to customize subtitle styling, this feature can
 *          only be implemented on "best effort" basis, and has to rely on
 *          heuristics that can easily break.
 * \param priv renderer handle
 * \param bits bit mask comprised of ASS_OverrideBits values.
 */
void ass_set_selective_style_override_enabled(ASS_Renderer *priv, int bits);

/**
 * \brief Set style for selective style override.
 * See ass_set_selective_style_override_enabled().
 * \param style style settings to use if override is enabled. Applications
 * should initialize it with {0} before setting fields. Strings will be copied
 * by the function.
 */
void ass_set_selective_style_override(ASS_Renderer *priv, ASS_Style *style);

/**
 * \brief This is a stub and does nothing. Old documentation: Update/build font
 * cache.  This needs to be called if it was disabled when ass_set_fonts was set.
 *
 * \param priv renderer handle
 * \return success
 */
ASS_DEPRECATED("it does nothing")
int ass_fonts_update(ASS_Renderer *priv);

/**
 * \brief Set hard cache limits.  Do not set, or set to zero, for reasonable
 * defaults.
 *
 * \param priv renderer handle
 * \param glyph_max maximum number of cached glyphs
 * \param bitmap_max_size maximum bitmap cache size (in MB)
 */
void ass_set_cache_limits(ASS_Renderer *priv, int glyph_max,
                          int bitmap_max_size);

/**
 * \brief Render a frame, producing a list of ASS_Image.
 * \param priv renderer handle
 * \param track subtitle track
 * \param now video timestamp in milliseconds
 * \param detect_change compare to the previous call and set to 1
 * if positions may have changed, or set to 2 if content may have changed.
 */
ASS_Image *ass_render_frame(ASS_Renderer *priv, ASS_Track *track,
                            long long now, int *detect_change);

/**
 * \brief Render a frame from a subset of a track's events.
 * \param priv renderer handle
 * \param track subtitle track
 * \param eids ids of the events that may take part; ids outside
 *        [0, n_events) are ignored, and each id should appear at most once
 * \param n_eids number of entries in eids (may be 0)
 * \param now video timestamp in milliseconds
 * \param flags 0 or ASS_RENDER_EVENTS_NO_COLLISIONS
 * \param detect_change as for ass_render_frame
 * \return the image list, or NULL if nothing is shown or an argument is
 *         invalid (unknown flags, n_eids < 0, eids NULL with n_eids > 0)
 *
 * Fork extension, not available in upstream libass; its presence is
 * indicated by LIBASS_HAS_RENDER_EVENTS.
 *
 * Works like ass_render_frame, but only the listed events that are active
 * at now are rendered. Collision placement runs among them as on a freshly
 * parsed track and neither reads nor writes the placement history that
 * ass_render_frame keeps in the events, so a track can be rendered with
 * this function in between ass_render_frame calls without changing what
 * those calls produce. A single event renders as ass_render_frame renders
 * a track that contains only that event. With
 * ASS_RENDER_EVENTS_NO_COLLISIONS, events are not moved to avoid each other
 * at all. Events are not pruned (see ass_configure_prune).
 *
 * The image list follows the same rules as one from ass_render_frame: it is
 * valid until the next ass_render_frame or ass_render_events call on this
 * renderer, and detect_change compares with the previous call of either.
 */
#define LIBASS_HAS_RENDER_EVENTS 1
#define ASS_RENDER_EVENTS_NO_COLLISIONS 1
ASS_Image *ass_render_events(ASS_Renderer *priv, ASS_Track *track,
                             const int *eids, int n_eids, long long now,
                             int flags, int *detect_change);

/**
 * 
 * \brief Callback used to observe positioned glyph runs after shaping/layout
 * and before bitmap rasterization.
 *
 * This is an experimental extension used by AkariSub's HarfBuzz GPU pipeline.
 * Coordinates and advances are in screen pixels after layout. Glyph extents
 * remain in HarfBuzz blob coordinate space; glyph_scale_x/y convert those
 * extents to screen pixels for quad placement.
 */
typedef void (*ASS_GlyphRunCallback)(void *user_data,
                                     const ASS_Event *event,
                                     int glyph_index,
                                     const void *font_handle,
                                     int face_index,
                                     int glyph_id,
                                     double x,
                                     double y,
                                     double advance_x,
                                     double advance_y,
                                     double glyph_scale_x,
                                     double glyph_scale_y,
                                     uint32_t primary_color_rgba,
                                     uint32_t outline_color_rgba,
                                     uint32_t shadow_color_rgba,
                                     double border_x,
                                     double border_y,
                                     double shadow_x,
                                     double shadow_y,
                                     double blur);

/**
 * \brief Register a glyph-run callback invoked during ass_render_frame().
 * 
 * Passing NULL disables callback delivery.
 */
void ass_set_glyph_run_callback(ASS_Renderer *priv,
                                ASS_GlyphRunCallback callback,
                                void *user_data);

/*
 * Frame atlas packing (fork extension).
 *
 * ass_frame_pack_atlas() copies the alpha bitmaps of an ASS_Image list into
 * one or more 8-bit texture pages and describes every bitmap with a fixed-size
 * instance record, so a GPU consumer can draw a whole frame with one texture
 * upload and one instanced draw call instead of one upload per image.
 *
 * LIBASS_HAS_FRAME_ATLAS is defined if ass_frame_pack_atlas() and
 * ass_frame_atlas_reset() are available.
 */
#define LIBASS_HAS_FRAME_ATLAS 1

/**
 * \brief One textured quad of a packed frame.
 *
 * The struct is exactly 32 bytes: eight consecutive 32-bit fields without
 * padding, so an array of them can be uploaded unchanged as a GPU vertex
 * buffer with a stride of 32 bytes:
 *   offset  0: dst_x, dst_y, w, h       (4 x int32)
 *   offset 16: atlas_x, atlas_y, page   (3 x int32)
 *   offset 28: color                    (uint32, same value as ASS_Image.color)
 * As bytes, color is stored in host byte order, i.e. on little-endian hosts
 * (including wasm32) the four bytes are A, B, G, R where A is the ASS
 * transparency (0 = opaque). A normalized unsigned-byte attribute therefore
 * yields vec4(A, B, G, R) / 255.
 *
 * The quad covers the video frame rectangle [dst_x, dst_x + w) x
 * [dst_y, dst_y + h); texel (i, j) of the quad is stored at
 * (atlas_x + i, atlas_y + j) of the given page. Blending is the same as for
 * the ASS_Image the instance was created from.
 */
typedef struct ass_atlas_instance {
    int32_t dst_x, dst_y;       // Placement inside the video frame
    int32_t w, h;               // Size in pixels, both > 0
    int32_t atlas_x, atlas_y;   // Top-left texel inside the page
    int32_t page;               // Page index, 0 <= page < ASS_Atlas.n_pages
    uint32_t color;             // Color and alpha, RGBA, as in ASS_Image
} ASS_AtlasInstance;

/**
 * \brief A packed frame, see ass_frame_pack_atlas().
 *
 * All pages have the same size and are stored back to back without gaps:
 * page i starts at buffer + i * width * height, and rows are tightly packed
 * (row stride == width bytes, no alignment padding; width is a multiple of 4
 * whenever max_size is, so the default unpack alignment of 4 also works
 * then). The buffer starts on a 64-byte boundary. The whole buffer is one
 * width x height x n_pages R8 volume, so it can be uploaded to a 2D array
 * texture in a single call (e.g. WebGL2 texSubImage3D with UNPACK_ALIGNMENT 1,
 * WebGPU writeTexture with bytesPerRow = width, rowsPerImage = height), or
 * page by page to plain 2D textures. A consumer may use textures larger than
 * width x height. Every byte of the buffer that is not covered by an instance
 * is zero.
 *
 * pixels_changed and instances_changed allow skipping redundant GPU work:
 * they are 0 only if the corresponding data is byte-for-byte identical to the
 * result of the previous successful ass_frame_pack_atlas() call on the same
 * renderer. They describe that previous libass result, not what the consumer
 * actually uploaded: a consumer that skipped or lost an upload (frame not
 * presented, texture reallocated, GPU context lost) must combine them with
 * its own dirty state, or call ass_frame_atlas_reset() so that the next
 * result reports both as 1.
 */
typedef struct ass_atlas {
    const ASS_AtlasInstance *instances; // n_instances quads in blend order
    int n_instances;
    int n_pages;                // Number of pages, 0 if n_instances == 0
    int width, height;          // Size of every page in pixels
    const unsigned char *buffer; // n_pages * width * height bytes
    int pixels_changed;         // 0 only if n_pages, width, height, buffer
                                // and its contents are unchanged since the
                                // previous successful call
    int instances_changed;      // 0 only if n_instances and the contents of
                                // instances are unchanged since the previous
                                // successful call
} ASS_Atlas;

/**
 * \brief Pack the bitmaps of an image list into a texture atlas.
 *
 * Instances are emitted in the order of the image list, which is the blend
 * order. Images with w <= 0, h <= 0, a NULL bitmap or a fully transparent
 * color (alpha byte 0xFF) produce no instance. Images larger than
 * max_size - 2 * padding in either dimension are split into tiles that are
 * emitted as consecutive instances; tiles never overlap, so compositing them
 * in any order gives the same result as compositing the untiled image (but
 * texture filtering across tile seams sees the padding, not the neighbor).
 * Otherwise there is exactly one instance per image.
 *
 * Placement is deterministic: it depends only on the sequence of image
 * (or tile) sizes, max_size and padding. Pages are filled with a shelf packer
 * that places taller images first; a new page is started only when a page
 * would exceed max_size x max_size. The page count is limited by max_pages;
 * a frame that does not fit fails cheaply, before the pixel buffer is
 * allocated. Small max_size values combined with large padding can need very
 * many pages, since every tile carries its own padding.
 *
 * When images is the list most recently returned by ass_render_frame() on
 * this renderer, or the frame of this renderer packed by the previous call,
 * and its bitmaps are the same as those of the previous call (only positions
 * or colors changed; this covers every frame for which ass_render_frame
 * reports detect_change < 2, and also color-only changes, which it reports as
 * 2), max_size and padding are the same, and the previous call succeeded on a
 * frame of this renderer that still fits in max_pages pages, the pixel buffer
 * is reused without copying and pixels_changed is 0. This detection is
 * automatic and does not rely on any caller-supplied hint. Images that
 * ass_render_frame() creates with their own bitmap on every frame (vector
 * clips, BorderStyle=4 boxes) never match, so frames containing them always
 * report pixels_changed = 1. To make it exact, the renderer keeps a reference
 * to the last packed frame's image list (and therefore its bitmaps) until the
 * next call, ass_frame_atlas_reset() or ass_renderer_done(); a call that
 * fails on that same list keeps the reference, so the list can still be drawn
 * directly. Bitmaps of frames returned by ass_render_frame() must be treated
 * as read-only, since reuse compares them by address only. Any other list,
 * including older frames of this renderer, is packed from scratch and not
 * referenced; its bitmaps must not be modified while it is being packed.
 *
 * Memory: all buffers are owned by the renderer and only grow, so packing a
 * frame of a size seen before allocates nothing. They are released by
 * ass_frame_atlas_reset() and ass_renderer_done(); a consumer that packed an
 * unusually large frame may call ass_frame_atlas_reset() to give the memory
 * back.
 *
 * \param priv renderer handle
 * \param images image list, e.g. as returned by ass_render_frame(); NULL is
 * an empty frame. Each bitmap must satisfy the ASS_Image contract and have
 * stride >= w.
 * \param max_size maximum page width and height in pixels, normally the
 * GPU's maximum texture size; values above 32768 are treated as 32768.
 * Must be greater than 2 * padding.
 * \param padding number of zero texels kept between neighbouring images and
 * at the page borders, 0 to 64. Use 0 with texelFetch-style unfiltered
 * access; use 1 or more if the consumer samples with filtering.
 * \param max_pages maximum number of pages, at least 1, normally the GPU's
 * maximum array texture layer count (e.g. WebGL2 MAX_ARRAY_TEXTURE_LAYERS,
 * WebGPU maxTextureArrayLayers). INT_MAX means no limit; only pass it if
 * max_size and padding are large enough to keep the page count reasonable,
 * since the page limit is also what bounds the buffer size.
 * \return the packed frame, or NULL on error (invalid arguments, an image
 * with stride < w, more than max_pages pages needed, size overflow or
 * allocation failure). On NULL the consumer can fall back to drawing the
 * image list directly, or retry with a larger max_size. The result and
 * everything it points to is owned by the renderer and stays valid until the
 * next ass_frame_pack_atlas() call, ass_frame_atlas_reset() or
 * ass_renderer_done(), whichever comes first; in particular it stays valid
 * across ass_render_frame(). After an error the previous result is invalid
 * too, and the next successful call reports pixels_changed =
 * instances_changed = 1.
 */
const ASS_Atlas *ass_frame_pack_atlas(ASS_Renderer *priv, ASS_Image *images,
                                      int max_size, int padding,
                                      int max_pages);

/**
 * \brief Drop the atlas state of a renderer.
 * Releases all memory used by ass_frame_pack_atlas() and the reference to
 * the last packed frame. The previous result becomes invalid, and the next
 * ass_frame_pack_atlas() call packs from scratch and reports
 * pixels_changed = instances_changed = 1. Use it when the consumer lost its
 * GPU copy of the atlas or wants to release memory. Does nothing if the
 * renderer has no atlas state.
 * \param priv renderer handle
 */
void ass_frame_atlas_reset(ASS_Renderer *priv);

/*
 * The following functions operate on track objects and do not need
 * an ass_renderer
 */

/**
 * \brief Allocate a new empty track object.
 * \param library handle
 * \return pointer to empty track or NULL on failure
 */
ASS_Track *ass_new_track(ASS_Library *);

/**
 * \brief Enable or disable certain features
 * This manages flags that control the behavior of the renderer and how certain
 * tags etc. within the track are interpreted. The defaults on a newly created
 * ASS_Track are such that rendering is compatible with traditional renderers
 * like VSFilter, and/or old versions of libass. Calling ass_process_data() or
 * ass_process_codec_private() may change some of these flags according to file
 * headers. (ass_process_chunk() will not change any of the flags.)
 * Additions to ASS_Feature are backward compatible to old libass releases (ABI
 * compatibility).
 * After calling ass_render_frame, changing features is no longer allowed.
 * \param track track
 * \param feature the specific feature to enable or disable
 * \param enable 0 for disable, any non-0 value for enable
 * \return 0 if feature set, -1 if feature is unknown
 */
int ass_track_set_feature(ASS_Track *track, ASS_Feature feature, int enable);

/**
 * \brief Deallocate track and all its child objects (styles and events).
 * \param track track to deallocate or NULL
 */
void ass_free_track(ASS_Track *track);

/**
 * \brief Allocate new style.
 * \param track track
 * \return newly allocated style id >= 0, or a value < 0 on failure
 * See GENERAL NOTE in ass_types.h.
 */
int ass_alloc_style(ASS_Track *track);

/**
 * \brief Allocate new event.
 * \param track track
 * \return newly allocated event id >= 0, or a value < 0 on failure
 * See GENERAL NOTE in ass_types.h.
 */
int ass_alloc_event(ASS_Track *track);

/**
 * \brief Delete a style.
 * \param track track
 * \param sid style id
 * Deallocates style data. Does not modify track->n_styles.
 * Freeing a style without subsequently setting track->n_styles
 * to a value less than or equal to the freed style id before calling
 * any other libass API function on the track is undefined behaviour.
 * Additionally a freed style style still being referenced by an event
 * in track->events will also result in undefined behaviour.
 * See GENERAL NOTE in ass_types.h.
 */
void ass_free_style(ASS_Track *track, int sid);

/**
 * \brief Delete an event.
 * \param track track
 * \param eid event id
 * Deallocates event data. Does not modify track->n_events.
 * Freeing an event without subsequently setting track->n_events
 * to a value less than or equal to the freed event id before calling
 * any other libass API function on the track is undefined behaviour.
 * See GENERAL NOTE in ass_types.h
 */
void ass_free_event(ASS_Track *track, int eid);

/**
 * \brief Parse full lines of subtitle stream data.
 * \param track track
 * \param data string to parse
 * \param size length of data
 */
void ass_process_data(ASS_Track *track, const char *data, int size);

/**
 * \brief Parse Codec Private section of the subtitle stream, in Matroska
 * format.  See the Matroska specification for details.
 * \param track target track
 * \param data string to parse
 * \param size length of data
 */
void ass_process_codec_private(ASS_Track *track, const char *data, int size);

/**
 * \brief Parse a chunk of subtitle stream data. A chunk contains exactly one
 * event in Matroska format.  See the Matroska specification for details.
 * In later libass versions (since LIBASS_VERSION==0x01300001), using this
 * function means you agree not to modify events manually, or using other
 * functions manipulating the event list like ass_process_data(). If you do
 * anyway, the internal duplicate checking might break. Calling
 * ass_flush_events() is still allowed.
 * \param track track
 * \param data string to parse
 * \param size length of data
 * \param timecode starting time of the event (milliseconds)
 * \param duration duration of the event (milliseconds)
 */
void ass_process_chunk(ASS_Track *track, const char *data, int size,
                       long long timecode, long long duration);

/**
 * \brief Set whether the ReadOrder field when processing a packet with
 * ass_process_chunk() should be used for eliminating duplicates.
 * \param check_readorder 0 means do not try to eliminate duplicates; 1 means
 * use the ReadOrder field embedded in the packet as unique identifier, and
 * discard the packet if there was already a packet with the same ReadOrder.
 * Other values are undefined.
 * If this function is not called, the default value is 1.
 */
void ass_set_check_readorder(ASS_Track *track, int check_readorder);

/**
 * \brief Prune events that preceed deadline.
 * \param track track
 * \param deadline cut-off timestamp in milliseconds.
*/
void ass_prune_events(ASS_Track *track, long long deadline);

/**
 * \brief Configure automatic pruning of events.
 * \param track track
 * \param delay delay from "now" (ass_render_frame) in milliseconds.
 * After every render, events whose undisplay timestamp predate
 * "now - delay" will be deleted. A delay of 0 prunes aggressively.
 * Negative delays disable automatic pruning.
 * Disabled by default (no removal of events from memory).
 */
void ass_configure_prune(ASS_Track *track, long long delay);

/**
 * \brief Flush buffered events.
 * \param track track
*/
void ass_flush_events(ASS_Track *track);

/**
 * \brief Notify libass of manual changes to the event list.
 * \param track track
 * \param first_eid lowest id of the changed events
 *
 * Fork extension, not available in upstream libass; its presence is
 * indicated by LIBASS_HAS_TRACK_EVENTS_MODIFIED.
 *
 * ass_render_frame keeps a per-track summary of event timing which lets it
 * skip events that are not displayed at the requested time. Changes made
 * through the libass API (ass_alloc_event, ass_free_event, ass_process_data,
 * ass_process_chunk, ass_flush_events, ass_prune_events, ...) are tracked
 * automatically, as is appending events by hand and raising n_events.
 * ass_free_event(track, eid) treats all events from eid on as changed, so
 * removing events with it needs no further notification, even if later
 * events are moved down to fill the gap and new ones are appended after.
 * Other manual changes after the first call to ass_render_frame on this
 * track are not tracked, so this function must be called after
 *   - modifying Start or Duration of an event with id >= first_eid, or
 *   - changing which event is stored at an id >= first_eid other than as
 *     described above, e.g. inserting events in the middle or reordering,
 * and before the next ass_render_frame call on this track. Otherwise
 * affected events may not be displayed when they should be.
 * Calling this function for other changes, or more often than necessary,
 * is harmless; ass_track_events_modified(track, 0) invalidates everything
 * and makes the next ass_render_frame rebuild the summary in O(n_events).
 * Negative first_eid is treated as 0.
 *
 * The summary is stored in the track and updated by ass_render_frame after
 * events were added or changed, so a track must not be rendered by several
 * threads at once until it has been rendered once after the last change.
 *
 * The GENERAL NOTE in ass_types.h still does not allow modifying existing
 * events after rendering started; this function only keeps event selection
 * in ass_render_frame correct for API users who do it anyway.
 */
#define LIBASS_HAS_TRACK_EVENTS_MODIFIED 1
void ass_track_events_modified(ASS_Track *track, int first_eid);

/**
 * \brief Replace a range of events with events parsed from script lines.
 * \param track track
 * \param first_eid id of the first event to replace (0 <= first_eid <= n_events)
 * \param n_remove number of events to remove from first_eid on
 *        (0 <= n_remove <= n_events - first_eid)
 * \param lines zero or more lines separated by \n or \r\n (or \r), in the
 *        same encoding (UTF-8) as the rest of the track; may be NULL if len is 0
 * \param len length of lines in bytes; parsing stops at a NUL byte
 * \return number of events inserted at first_eid (>= 0), or -1 if an argument
 *         is invalid, the track has no event format, or memory could not be
 *         allocated; the track is unchanged on failure
 *
 * Fork extension, not available in upstream libass; its presence is
 * indicated by LIBASS_HAS_TRACK_SPLICE_EVENTS.
 *
 * Every "Dialogue:" line is parsed with the track's current event format
 * exactly as ass_read_memory parses it in the [Events] section (style lookup
 * by name, End and Actor aliases, time values, trimming of trailing white
 * space from Text). All other lines, including "Comment:", "Format:" and
 * section headers, yield no event and do not change the track; malformed
 * Dialogue lines that ass_read_memory drops are dropped here too. Callers
 * that need one event per line should compare the return value with the
 * number of Dialogue lines they passed and fall back to a full parse if
 * they differ.
 *
 * ReadOrder of the inserted events, and of all later events if their ids
 * moved, is set to their event id, as ass_read_memory numbers events.
 * This function then does what
 * ass_track_events_modified(track, first_eid) and
 * ass_track_reset_layout_state(track) do, so the next ass_render_frame call
 * renders the track exactly as it would render a track freshly parsed by
 * ass_read_memory from the edited script, as long as only Dialogue and
 * Comment lines in [Events] changed and the event Format line did not.
 *
 * Meant for tracks read with ass_read_memory or ass_read_file. Must not be
 * called while another thread renders the track.
 */
#define LIBASS_HAS_TRACK_SPLICE_EVENTS 1
int ass_track_splice_events(ASS_Track *track, int first_eid, int n_remove,
                            const char *lines, size_t len);

/**
 * \brief Forget the collision placement history of all events of a track.
 * \param track track
 *
 * Fork extension, not available in upstream libass; its presence is
 * indicated by LIBASS_HAS_TRACK_SPLICE_EVENTS.
 *
 * ass_render_frame remembers where it placed each event to avoid collisions
 * and keeps that position while the event stays on screen. After this call
 * the next frame places events as on a freshly parsed track. This takes
 * constant time. ass_track_splice_events calls it itself; call it after
 * changing events or styles in other ways if rendering should match a fresh
 * parse of the edited script.
 */
void ass_track_reset_layout_state(ASS_Track *track);

/**
 * \brief Replace a style with one parsed from a Style line.
 * \param track track
 * \param sid id of the style to replace (1 <= sid < n_styles; style 0 is
 *        libass's built-in default style, which no script line describes)
 * \param line one "Style: ..." line as it appears in the script, in UTF-8;
 *        anything after the first line break is ignored
 * \param len length of line in bytes
 * \return 0 on success, -1 if an argument is invalid, the line is not a
 *         Style line, its Name differs from the current name of style sid,
 *         or memory could not be allocated; the track is unchanged on failure
 *
 * Fork extension, not available in upstream libass; its presence is
 * indicated by LIBASS_HAS_TRACK_SET_STYLE.
 *
 * The line is parsed with the track's current style format exactly as
 * ass_read_memory parses it, and the library's style overrides (see
 * ass_set_style_overrides) for this style are applied again. Events refer to
 * styles by the index their style name resolved to when they were parsed, so
 * a style cannot be renamed this way; renaming, adding or removing styles
 * needs a full parse. Collision placement history is reset as with
 * ass_track_reset_layout_state, so the next ass_render_frame call renders
 * the track exactly as it would render a track freshly parsed from the
 * edited script. With several styles of the same name, edit the one at the
 * same position: events use the last of them, as ass_read_memory resolves
 * names.
 *
 * Must not be called while another thread renders the track.
 */
#define LIBASS_HAS_TRACK_SET_STYLE 1
int ass_track_set_style(ASS_Track *track, int sid, const char *line, size_t len);

/**
 * \brief Read subtitles from file.
 * \param library library handle
 * \param fname file name
 * \param codepage encoding (iconv format)
 * \return newly allocated track or NULL on failure
 * NOTE: On Microsoft Windows, when using WIN32-APIs, fname must be in either
 * UTF-8 mixed with lone or paired UTF-16 surrogates encoded like in CESU-8
 * or the encoding accepted by fopen with the former taking precedence
 * if both versions are valid and exist.
 * On all other systems there is no need for special considerations like that.
*/
ASS_Track *ass_read_file(ASS_Library *library, const char *fname,
                         const char *codepage);

/**
 * \brief Read subtitles from memory.
 * \param library library handle
 * \param buf pointer to subtitles text
 * \param bufsize size of buffer
 * \param codepage encoding (iconv format)
 * \return newly allocated track or NULL on failure
*/
ASS_Track *ass_read_memory(ASS_Library *library, char *buf,
                           size_t bufsize, const char *codepage);
/**
 * \brief Read styles from file into already initialized track.
 * \param fname file name
 * \param codepage encoding (iconv format)
 * \return 0 on success
 * NOTE: On Microsoft Windows, when using WIN32-APIs, fname must be in either
 * UTF-8 mixed with lone or paired UTF-16 surrogates encoded like in CESU-8
 * or the encoding accepted by fopen with the former taking precedence
 * if both versions are valid and exist.
 * On all other systems there is no need for special considerations like that.
 */
int ass_read_styles(ASS_Track *track, const char *fname, const char *codepage);

/**
 * \brief Add a memory font.
 * \param library library handle
 * \param name attachment name
 * \param data binary font data
 * \param data_size data size
*/
void ass_add_font(ASS_Library *library, const char *name, const char *data,
                  int data_size);

/**
 * \brief Remove all fonts stored in an ass_library object.
 * This can only be called safely if all ASS_Track and ASS_Renderer instances
 * associated with the library handle have been released first.
 * \param library library handle
 */
void ass_clear_fonts(ASS_Library *library);

/**
 * \brief Calculates timeshift from now to the start of some other subtitle
 * event, depending on movement parameter.
 * \param track subtitle track
 * \param now current time in milliseconds
 * \param movement how many events to skip from the one currently displayed
 * +2 means "the one after the next", -1 means "previous"
 * \return timeshift in milliseconds
 */
long long ass_step_sub(ASS_Track *track, long long now, int movement);

/**
 * \brief Allocates memory that can be safely freed by libass later.
 * Use this to allocate buffers you'll use to manually modify ASS_Track events
 * or related structures.
 * \param size number of bytes to allocate
 * \return pointer to the allocated buffer, or NULL on failure
 */
void *ass_malloc(size_t size);

/**
 * \brief Releases memory previously allocated within libass.
 * Use this to free memory allocated using ass_malloc.
 * \param ptr pointer to the buffer to release; passing NULL is a no-op
 */
void ass_free(void *ptr);

#undef ASS_DEPRECATED
#undef ASS_DEPRECATED_ENUM

#ifdef __cplusplus
}
#endif

#endif /* LIBASS_ASS_H */
