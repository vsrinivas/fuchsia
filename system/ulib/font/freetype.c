#include <font/font.h>

#include "inconsolata.h"

#include <gfx/gfx.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

static bool s_initialized = false;
static bool s_initialization_succeeded = false;
FT_Library s_library;
FT_Face s_face;

#undef __FTERRORS_H__
#define FT_ERRORDEF(e, v, s)  { e, s \
},
#define FT_ERROR_START_LIST     {
#define FT_ERROR_END_LIST       { 0, 0 } \
};

const struct
{
    int err_code;
    const char*  err_msg;
}

ft_errors[] =

#include FT_ERRORS_H


static void report_ft_error(const char* label, FT_Error error)
{
    if (error != 0) {
        printf("FreeType error: %s: %s\n", label, ft_errors[error].err_msg);
    }
}

int from26Dot6(FT_F26Dot6 v)
{
    return v / 64;
}

inline FT_F26Dot6 to26Dot6(int v)
{
    return v * 64;
}

static uint8_t sRenderedGlyphs[FONT_X * FONT_Y * 256];

static void render_glyph(uint8_t* target, int i)
{
    FT_UInt glyph_index = FT_Get_Char_Index( s_face, i);
    if (glyph_index == 0) {
        return;
    }
    FT_Error error = FT_Load_Glyph(s_face, glyph_index, FT_LOAD_DEFAULT);
    if (error != 0) {
        return;
    }
    FT_GlyphSlot slot = s_face->glyph;
    error = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
    if (error != 0) {
        return;
    }

    FT_Glyph glyph;
    error = FT_Get_Glyph(s_face->glyph, &glyph);
    if (error != 0) {
        return;
    }
    
    if (glyph->format != FT_GLYPH_FORMAT_BITMAP) {
        return;
    }
    FT_BitmapGlyph bitmapGlyph = (FT_BitmapGlyph)glyph;
    FT_Bitmap* bitMap = &bitmapGlyph->bitmap;
    int descender = from26Dot6(s_face->size->metrics.descender);
    int horiBearingY = from26Dot6(slot->metrics.horiBearingY);
    int horiBearingX = from26Dot6(slot->metrics.horiBearingX);
    int deltaY = horiBearingY - descender + 2;
    for (int i = 0; i < (int)bitMap->rows; ++i) {
        for (int j = 0; j < (int)bitMap->width; ++j) {
            uint8_t gray_value = bitMap->buffer[i * bitMap->width + j];
            int y_pos = i + FONT_Y - deltaY;
            int x_pos = j + horiBearingX;
            if (y_pos >= 0) {
                target[y_pos*FONT_X + x_pos] = gray_value;
            }
        }
    }
}

bool initialize_freetype(void)
{
    printf("initialize_freetype...\n");

    if (s_initialized) {
        return s_initialization_succeeded;
    }
    
    s_initialized = true;
    
    FT_Error error = FT_Init_FreeType(&s_library);
    if (error != 0) {
        report_ft_error("FT_Init_FreeType", error);
        goto done;
    }
    
    error = FT_New_Memory_Face(s_library,
                    external_ulib_freetype_Inconsolata_Regular_ttf,
                    external_ulib_freetype_Inconsolata_Regular_ttf_len,
                     0,
                     &s_face);
    if (error != 0) {
        report_ft_error("FT_New_Face", error);
        goto done;
    }
    
    error = FT_Set_Pixel_Sizes(s_face, 0, FONT_Y);
    if (error != 0) {
        report_ft_error("FT_Set_Pixel_Sizes", error);
        goto done;
    }

    memset(sRenderedGlyphs, 0, sizeof(sRenderedGlyphs));
    
    uint8_t* p = sRenderedGlyphs;
    for (int i = 0; i < 256; ++i) {
        render_glyph(p, i);
        p += (FONT_X * FONT_Y);
    }

    s_initialization_succeeded = true;
    printf("initialize_freetype suceeded.\n");
    return true;

done:
    return s_initialization_succeeded;
}

uint32_t blend(uint32_t color, uint32_t bgcolor, uint8_t alpha)
{
    double alpha_ratio = (double)alpha/256.0;
    
    uint32_t blue  = (color & 0xFF);
    uint32_t green = ((color >> 8) & 0xFF);
    uint32_t red   = ((color >> 16) & 0xFF);

    uint32_t bg_blue  = (bgcolor & 0xFF);
    uint32_t bg_green = ((bgcolor >> 8) & 0xFF);
    uint32_t bg_red   = ((bgcolor >> 16) & 0xFF);
    
    uint32_t r_blue = alpha_ratio * blue;
    uint32_t r_green = alpha_ratio * green;
    uint32_t r_red = alpha_ratio * red;
    
    double inv_alpha = 1 - alpha_ratio;
    
    r_blue += (bg_blue * inv_alpha);
    r_green += (bg_green * inv_alpha);
    r_red += (bg_red * inv_alpha);
    
    return 0xff000000 | r_blue | (r_green << 8) | (r_red << 16);
}

bool freetype_draw_char(gfx_surface *surface, unsigned char c, int x, int y, uint32_t color, uint32_t bgcolor)
{
    if (!s_initialized) {
        if (!initialize_freetype()) {
            return false;
        }
    }
    
    if (!s_initialization_succeeded) {
        return false;
    }
    
    uint8_t* p = sRenderedGlyphs + (c * FONT_Y * FONT_X);
    for (int i = 0; i < FONT_Y; i++) {
        for (int j = 0; j < FONT_X; j++) {
            uint8_t gray_value = *p++;
            gfx_putpixel(surface, x + j, y + i, blend(color, bgcolor, gray_value));
        }
    }
    return true;
}

