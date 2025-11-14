/*
 * Copyright (c) 2010-2025 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "truetypefont.h"

#include "image.h"
#include "framework/core/resourcemanager.h"
#include "framework/stdext/exception.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#include <fmt/format.h>
#include <algorithm>
#include <vector>
#include <cmath>

TrueTypeFontBuildResult TrueTypeFontBuilder::build(const TrueTypeFontSettings& settings) const
{
    if (settings.file.empty())
        throw stdext::exception("TrueTypeFontBuilder: missing ttf file path");

    std::string resolvedPath = settings.file;
    if (!g_resources.fileExists(resolvedPath))
        resolvedPath = g_resources.guessFilePath(settings.file, "ttf");
    const auto& buffer = g_resources.readFileContents(resolvedPath);
    if (buffer.empty())
        throw stdext::exception(fmt::format("TrueType font '{}' is empty", resolvedPath));

    const auto* fontData = reinterpret_cast<const unsigned char*>(buffer.data());
    const int fontOffset = stbtt_GetFontOffsetForIndex(fontData, 0);
    if (fontOffset < 0)
        throw stdext::exception(fmt::format("Failed to locate TrueType font '{}'", resolvedPath));

    stbtt_fontinfo fontInfo{};
    if (!stbtt_InitFont(&fontInfo, fontData, fontOffset))
        throw stdext::exception(fmt::format("Failed to parse TrueType font '{}'", resolvedPath));

    const int firstGlyph = std::clamp(settings.firstGlyph, 0, 255);
    const int lastGlyph = std::clamp(settings.lastGlyph, firstGlyph, 255);
    const int glyphCount = std::max(0, lastGlyph - firstGlyph + 1);
    if (glyphCount == 0)
        throw stdext::exception("TrueTypeFontBuilder: invalid glyph range");

    const int atlasWidth = std::max(64, settings.atlasSize.width());
    const int atlasHeight = std::max(64, settings.atlasSize.height());

    std::vector<uint8_t> atlasData(static_cast<size_t>(atlasWidth) * atlasHeight, 0);
    std::vector<stbtt_packedchar> packedChars(glyphCount);

    stbtt_pack_context packContext;
    if (stbtt_PackBegin(&packContext, atlasData.data(), atlasWidth, atlasHeight, 0, settings.padding, nullptr) == 0)
        throw stdext::exception(fmt::format("Failed to start packing for font '{}'", resolvedPath));

    const int oversampleX = std::clamp(settings.oversample.width(), 1, 8);
    const int oversampleY = std::clamp(settings.oversample.height(), 1, 8);
    stbtt_PackSetOversampling(&packContext, oversampleX, oversampleY);

    if (stbtt_PackFontRange(&packContext,
                            fontData,
                            0,
                            settings.pixelSize,
                            firstGlyph,
                            glyphCount,
                            packedChars.data()) == 0) {
        stbtt_PackEnd(&packContext);
        throw stdext::exception(fmt::format("Unable to pack glyphs for TrueType font '{}'", resolvedPath));
    }

    stbtt_PackEnd(&packContext);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
    const float scale = stbtt_ScaleForPixelHeight(&fontInfo, settings.pixelSize);
    const int baseline = std::lround(ascent * scale);
    const int lineHeight = std::lround((ascent - descent + lineGap) * scale);

    std::array<int, 256> glyphIndices{};
    for (int cp = firstGlyph; cp <= lastGlyph; ++cp)
        glyphIndices[cp] = stbtt_FindGlyphIndex(&fontInfo, cp);

    std::vector<uint8_t> rgba(static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0);
    for (size_t i = 0, j = 0; i < atlasData.size(); ++i, j += 4) {
        const uint8_t alpha = atlasData[i];
        rgba[j    ] = 255;
        rgba[j + 1] = 255;
        rgba[j + 2] = 255;
        rgba[j + 3] = alpha;
    }

    const auto& atlasImage = std::make_shared<Image>(Size(atlasWidth, atlasHeight), 4, rgba.data());

    TrueTypeFontBuildResult result;
    result.atlasImage = atlasImage;
    result.glyphHeight = lineHeight;
    result.baseline = baseline;

    for (int i = 0; i < glyphCount; ++i) {
        const int glyph = firstGlyph + i;
        const stbtt_packedchar& ch = packedChars[i];

        const int width = static_cast<int>(ch.x1 - ch.x0);
        const int height = static_cast<int>(ch.y1 - ch.y0);

        result.glyphSize[glyph].resize(width, height);
        result.textureCoords[glyph].setRect(ch.x0, ch.y0, width, height);
        result.glyphOffset[glyph] = Point(static_cast<int>(std::floor(ch.xoff)), static_cast<int>(std::floor(ch.yoff)));
        result.glyphAdvance[glyph] = static_cast<int>(std::lround(ch.xadvance));
    }

    for (int a = firstGlyph; a <= lastGlyph; ++a) {
        const int glyphA = glyphIndices[a];
        if (glyphA == 0)
            continue;
        for (int b = firstGlyph; b <= lastGlyph; ++b) {
            const int glyphB = glyphIndices[b];
            if (glyphB == 0)
                continue;
            const int kern = stbtt_GetGlyphKernAdvance(&fontInfo, glyphA, glyphB);
            if (kern != 0) {
                const int value = std::lround(kern * scale);
                result.kerning[a][b] = std::clamp(value, -32768, 32767);
            }
        }
    }

    return result;
}

