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

#include "bitmapfont.h"

#include "drawpoolmanager.h"
#include "image.h"
#include "painter.h"
#include "texture.h"
#include "truetypefont.h"
#include <algorithm>
#include "textureatlas.h"
#include "texturemanager.h"
#include "framework/otml/otmlnode.h"

static thread_local std::vector<Point> s_glyphsPositions(1);
static thread_local std::vector<int>   s_lineWidths(1);

void BitmapFont::load(const OTMLNodePtr& fontNode)
{
    for (auto& row : m_kerning)
        row.fill(0);

    m_glyphSpacing = fontNode->valueAt("spacing", Size(0));
    m_firstGlyph = fontNode->valueAt("first-glyph", 32);
    const bool hasCustomYOffset = fontNode->hasChildAt("y-offset");
    const int customYOffset = hasCustomYOffset ? fontNode->valueAt<int>("y-offset") : 0;
    m_yOffset = customYOffset;

    m_outlineThickness = std::max(0, fontNode->valueAt<int>("outline-thickness", 0));
    m_outlineColor = fontNode->valueAt<Color>("outline-color", Color::black);
    updateOutlineOffsets();

    if (fontNode->hasChildAt("ttf-file")) {
        const auto fontFile = stdext::resolve_path(fontNode->valueAt<std::string>("ttf-file"), fontNode->source());

        TrueTypeFontSettings settings;
        settings.file = fontFile;
        const int defaultHeight = fontNode->valueAt("height", 16);
        settings.pixelSize = std::max(1.f, static_cast<float>(fontNode->valueAt<double>("font-size", static_cast<double>(defaultHeight))));
        settings.atlasSize = fontNode->valueAt("atlas-size", Size(1024, 1024));
        settings.oversample = fontNode->valueAt("oversample", Size(1, 1));
        settings.padding = fontNode->valueAt("ttf-padding", 1);

        if (fontNode->hasChildAt("glyph-range")) {
            const auto range = fontNode->valueAt<Point>("glyph-range");
            settings.firstGlyph = std::clamp(range.x, 0, 255);
            settings.lastGlyph = std::clamp(range.y, settings.firstGlyph, 255);
        } else {
            settings.firstGlyph = m_firstGlyph;
            settings.lastGlyph = std::clamp(fontNode->valueAt("last-glyph", 255), settings.firstGlyph, 255);
        }

        TrueTypeFontBuilder builder;
        const auto result = builder.build(settings);

        m_texture = std::make_shared<Texture>(result.atlasImage, false, false);
        m_texture->allowAtlasCache();
        m_texture->create();

        m_glyphHeight = fontNode->valueAt<int>("height", result.glyphHeight);

        int minYOffset = 0;
        int maxYOffset = 0;
        for (int glyph = 0; glyph < 256; ++glyph) {
            m_glyphsSize[glyph] = result.glyphSize[glyph];
            m_glyphsTextureCoords[glyph] = result.textureCoords[glyph];
            m_glyphsOffset[glyph] = result.glyphOffset[glyph];
            m_glyphsAdvance[glyph] = result.glyphAdvance[glyph];
            minYOffset = std::min(minYOffset, m_glyphsOffset[glyph].y);
            maxYOffset = std::max(maxYOffset, m_glyphsOffset[glyph].y + m_glyphsSize[glyph].height());
        }
        m_kerning = result.kerning;
        const int yShift = -minYOffset;
        for (auto& offset : m_glyphsOffset)
            offset.y += yShift;

        m_yOffset = hasCustomYOffset ? customYOffset + yShift : yShift;
        m_glyphHeight = std::max(m_glyphHeight, maxYOffset - minYOffset);

        m_glyphsSize[127].setWidth(1);
        m_glyphsAdvance[127] = 1;
        m_glyphsSize[static_cast<uint8_t>('\n')] = { 1, m_glyphHeight };
        m_glyphsAdvance[static_cast<uint8_t>('\n')] = 0;

        if (m_glyphsSize[32].width() == 0) {
            const int fallbackSpace = std::max(m_glyphsAdvance[32], m_glyphHeight / 4);
            m_glyphsSize[32].setWidth(fallbackSpace);
            m_glyphsAdvance[32] = fallbackSpace;
        }

        const int spaceWidth = fontNode->valueAt("space-width", m_glyphsAdvance[32]);
        m_glyphsSize[32].setWidth(spaceWidth);
        m_glyphsAdvance[32] = spaceWidth;

        return;
    }

    const auto& textureNode = fontNode->at("texture");
    const auto& textureFile = stdext::resolve_path(textureNode->value(), textureNode->source());
    const auto& glyphSize = fontNode->valueAt<Size>("glyph-size");
    const int spaceWidth = fontNode->valueAt("space-width", glyphSize.width());

    m_glyphHeight = fontNode->valueAt<int>("height");

    m_texture = g_textures.getTexture(textureFile, false);
    if (!m_texture)
        return;
    m_texture->create();

    const Size textureSize = m_texture->getSize();

    if (const auto& node = fontNode->get("fixed-glyph-width")) {
        for (int glyph = m_firstGlyph; glyph < 256; ++glyph)
            m_glyphsSize[glyph] = Size(node->value<int>(), m_glyphHeight);
    } else {
        calculateGlyphsWidthsAutomatically(Image::load(textureFile), glyphSize);
    }

    m_glyphsSize[32].setWidth(spaceWidth);
    m_glyphsAdvance[32] = spaceWidth;
    m_glyphsSize[127].setWidth(1);
    m_glyphsAdvance[127] = 1;
    m_glyphsSize[static_cast<uint8_t>('\n')] = { 1, m_glyphHeight };
    m_glyphsAdvance[static_cast<uint8_t>('\n')] = 0;

    const int numHorizontalGlyphs = textureSize.width() / glyphSize.width();
    for (int glyph = m_firstGlyph; glyph < 256; ++glyph) {
        m_glyphsTextureCoords[glyph].setRect(((glyph - m_firstGlyph) % numHorizontalGlyphs) * glyphSize.width(),
                                             ((glyph - m_firstGlyph) / numHorizontalGlyphs) * glyphSize.height(),
                                             m_glyphsSize[glyph].width(),
                                             m_glyphHeight);
        m_glyphsOffset[glyph] = Point(0, 0);
        m_glyphsAdvance[glyph] = m_glyphsSize[glyph].width();
    }

    for (int glyph = 0; glyph < 32; ++glyph) {
        m_glyphsOffset[glyph] = Point(0, 0);
        m_glyphsAdvance[glyph] = m_glyphsSize[glyph].width();
    }
}

void BitmapFont::drawText(const std::string_view text, const Point& startPos, const Color& color)
{
    const Size boxSize = g_painter->getResolution() - startPos.toSize();
    const Rect screenCoords(startPos, boxSize);
    drawText(text, screenCoords, color, Fw::AlignTopLeft);
}

void BitmapFont::drawText(const std::string_view text, const Rect& screenCoords, const Color& color, const Fw::AlignmentFlag align)
{
    Size textBoxSize;
    calculateGlyphsPositions(text, align, s_glyphsPositions, &textBoxSize);

    auto pairs = getDrawTextCoords(text, textBoxSize, align, screenCoords, s_glyphsPositions);

    if (hasOutline()) {
        for (const auto& offset : m_outlineOffsets) {
            if (offset.isNull())
                continue;
            for (const auto& p : pairs) {
                Rect outlineRect = p.first;
                outlineRect.translate(offset);
                g_drawPool.addTexturedRect(outlineRect, m_texture, p.second, m_outlineColor);
            }
        }
    }

    for (const auto& p : pairs) {
        g_drawPool.addTexturedRect(p.first, m_texture, p.second, color);
    }
}

inline bool BitmapFont::clipAndTranslateGlyph(Rect& glyphScreenCoords, Rect& glyphTextureCoords, const Rect& screenCoords) const noexcept
{
    if (glyphScreenCoords.bottom() < 0 || glyphScreenCoords.right() < 0)
        return false;

    if (glyphScreenCoords.top() < 0) {
        glyphTextureCoords.setTop(glyphTextureCoords.top() - glyphScreenCoords.top());
        glyphScreenCoords.setTop(0);
    }
    if (glyphScreenCoords.left() < 0) {
        glyphTextureCoords.setLeft(glyphTextureCoords.left() - glyphScreenCoords.left());
        glyphScreenCoords.setLeft(0);
    }

    glyphScreenCoords.translate(screenCoords.topLeft());

    if (!screenCoords.intersects(glyphScreenCoords))
        return false;

    if (glyphScreenCoords.bottom() > screenCoords.bottom()) {
        glyphTextureCoords.setBottom(glyphTextureCoords.bottom() + (screenCoords.bottom() - glyphScreenCoords.bottom()));
        glyphScreenCoords.setBottom(screenCoords.bottom());
    }
    if (glyphScreenCoords.right() > screenCoords.right()) {
        glyphTextureCoords.setRight(glyphTextureCoords.right() + (screenCoords.right() - glyphScreenCoords.right()));
        glyphScreenCoords.setRight(screenCoords.right());
    }

    return true;
}

std::vector<std::pair<Rect, Rect>> BitmapFont::getDrawTextCoords(const std::string_view text,
                                                                 const Size& textBoxSize,
                                                                 const Fw::AlignmentFlag align,
                                                                 const Rect& screenCoords,
                                                                 const std::vector<Point>& glyphsPositions) const noexcept
{
    std::vector<std::pair<Rect, Rect>> list;
    if (!screenCoords.isValid() || !m_texture)
        return list;

    const int textLength = static_cast<int>(text.length());
    list.reserve(textLength);

    int dx = 0;
    int dy = 0;

    if (align & Fw::AlignBottom) {
        dy = screenCoords.height() - textBoxSize.height();
    } else if (align & Fw::AlignVerticalCenter) {
        dy = (screenCoords.height() - textBoxSize.height()) / 2;
    }

    if (align & Fw::AlignRight) {
        dx = screenCoords.width() - textBoxSize.width();
    } else if (align & Fw::AlignHorizontalCenter) {
        dx = (screenCoords.width() - textBoxSize.width()) / 2;
    }

    const AtlasRegion* region = m_texture->getAtlasRegion();

    for (int i = 0; i < textLength; ++i) {
        const int glyph = static_cast<uint8_t>(text[i]);
        if (glyph < 32) continue;

        Rect glyphScreenCoords(glyphsPositions[i] + Point(dx, dy) + m_glyphsOffset[glyph], m_glyphsSize[glyph]);
        Rect glyphTextureCoords = m_glyphsTextureCoords[glyph];

        if (!clipAndTranslateGlyph(glyphScreenCoords, glyphTextureCoords, screenCoords))
            continue;

        if (region)
            glyphTextureCoords.translate(region->x, region->y);

        list.emplace_back(glyphScreenCoords, glyphTextureCoords);
    }

    return list;
}

void BitmapFont::fillTextCoords(const CoordsBufferPtr& coords, const std::string_view text,
                                const Size& textBoxSize, const Fw::AlignmentFlag align, const Rect& screenCoords,
                                const std::vector<Point>& glyphsPositions) const noexcept
{
    coords->clear();
    if (!screenCoords.isValid() || !m_texture)
        return;

    const int textLength = static_cast<int>(text.length());

    int dx = 0;
    int dy = 0;

    if (align & Fw::AlignBottom) {
        dy = screenCoords.height() - textBoxSize.height();
    } else if (align & Fw::AlignVerticalCenter) {
        dy = (screenCoords.height() - textBoxSize.height()) / 2;
    }

    if (align & Fw::AlignRight) {
        dx = screenCoords.width() - textBoxSize.width();
    } else if (align & Fw::AlignHorizontalCenter) {
        dx = (screenCoords.width() - textBoxSize.width()) / 2;
    }

    const AtlasRegion* region = m_texture->getAtlasRegion();

    for (int i = 0; i < textLength; ++i) {
        const int glyph = static_cast<uint8_t>(text[i]);
        if (glyph < 32) continue;

        Rect glyphScreenCoords(glyphsPositions[i] + Point(dx, dy) + m_glyphsOffset[glyph], m_glyphsSize[glyph]);
        Rect glyphTextureCoords = m_glyphsTextureCoords[glyph];

        if (!clipAndTranslateGlyph(glyphScreenCoords, glyphTextureCoords, screenCoords))
            continue;

        if (region)
            glyphTextureCoords.translate(region->x, region->y);

        coords->addRect(glyphScreenCoords, glyphTextureCoords);
    }
}

void BitmapFont::fillTextColorCoords(std::vector<std::pair<Color, CoordsBufferPtr>>& colorCoords, const std::string_view text,
                                     const std::vector<std::pair<int, Color>> textColors,
                                     const Size& textBoxSize, const Fw::AlignmentFlag align,
                                     const Rect& screenCoords, const std::vector<Point>& glyphsPositions) const noexcept
{
    colorCoords.clear();
    if (!screenCoords.isValid() || !m_texture)
        return;

    if (hasOutline()) {
        for (const auto& offset : m_outlineOffsets) {
            if (offset.isNull())
                continue;
            auto buffer = std::make_shared<CoordsBuffer>();
            fillTextCoords(buffer, text, textBoxSize, align, screenCoords.translated(offset), glyphsPositions);
            colorCoords.emplace_back(m_outlineColor, buffer);
        }
    }

    const int textLength = static_cast<int>(text.length());
    const int textColorsSize = static_cast<int>(textColors.size());

    std::unordered_map<uint32_t, CoordsBufferPtr> colorCoordsMap;
    uint32_t curColorRgba = 0;
    int32_t nextColorIndex = 0;
    int32_t colorIndex = -1;
    CoordsBufferPtr coords;

    for (int i = 0; i < textLength; ++i) {
        if (i >= nextColorIndex) {
            colorIndex = colorIndex + 1;
            if (colorIndex < textColorsSize) {
                curColorRgba = textColors[colorIndex].second.rgba();
            }
            if (colorIndex + 1 < textColorsSize) {
                nextColorIndex = textColors[colorIndex + 1].first;
            } else {
                nextColorIndex = textLength;
            }

            auto it = colorCoordsMap.find(curColorRgba);
            if (it == colorCoordsMap.end()) {
                coords = std::make_shared<CoordsBuffer>();
                colorCoordsMap.emplace(curColorRgba, coords);
            } else {
                coords = it->second;
            }
        }

        const int glyph = static_cast<uint8_t>(text[i]);
        if (glyph < 32) continue;

        Rect glyphScreenCoords(glyphsPositions[i] + m_glyphsOffset[glyph], m_glyphsSize[glyph]);
        Rect glyphTextureCoords = m_glyphsTextureCoords[glyph];

        int dx = 0, dy = 0;
        if (align & Fw::AlignBottom) {
            dy = screenCoords.height() - textBoxSize.height();
        } else if (align & Fw::AlignVerticalCenter) {
            dy = (screenCoords.height() - textBoxSize.height()) / 2;
        }
        if (align & Fw::AlignRight) {
            dx = screenCoords.width() - textBoxSize.width();
        } else if (align & Fw::AlignHorizontalCenter) {
            dx = (screenCoords.width() - textBoxSize.width()) / 2;
        }

        glyphScreenCoords.translate(dx, dy);

        if (!clipAndTranslateGlyph(glyphScreenCoords, glyphTextureCoords, screenCoords))
            continue;

        if (const AtlasRegion* region = m_texture->getAtlasRegion())
            glyphTextureCoords.translate(region->x, region->y);

        coords->addRect(glyphScreenCoords, glyphTextureCoords);
    }

    colorCoords.reserve(colorCoords.size() + colorCoordsMap.size());
    for (auto& kv : colorCoordsMap) {
        colorCoords.emplace_back(Color(kv.first), kv.second);
    }
}

void BitmapFont::calculateGlyphsPositions(std::string_view text,
                                          Fw::AlignmentFlag align,
                                          std::vector<Point>& glyphsPositions,
                                          Size* textBoxSize) const noexcept
{
    const int textLength = static_cast<int>(text.size());
    int maxLineWidth = 0;
    int lines = 0;

    if (textBoxSize && textLength == 0) {
        textBoxSize->resize(0, m_glyphHeight);
        return;
    }

    if (static_cast<int>(glyphsPositions.size()) < textLength)
        glyphsPositions.resize(textLength);
    if (static_cast<int>(glyphsPositions.capacity()) < textLength)
        glyphsPositions.reserve(std::max(1024, textLength));

    const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data());
    const int* __restrict advances = m_glyphsAdvance;

    const bool needLines =
        (align & Fw::AlignRight) || (align & Fw::AlignHorizontalCenter) || (textBoxSize != nullptr);

    if (needLines) {
        if (s_lineWidths.empty()) s_lineWidths.resize(1);
        s_lineWidths[0] = 0;

        int prevGlyph = -1;
        for (int i = 0; i < textLength; ++i) {
            const unsigned char g = p[i];
            if (g == static_cast<unsigned char>('\n')) {
                ++lines;
                if (lines + 1 > static_cast<int>(s_lineWidths.size()))
                    s_lineWidths.resize(lines + 1);
                s_lineWidths[lines] = 0;
                prevGlyph = -1;
                continue;
            }
            if (g >= 32) {
                if (prevGlyph >= 0)
                    s_lineWidths[lines] += m_kerning[prevGlyph][g];
                s_lineWidths[lines] += advances[g];
                if (i + 1 != textLength && p[i + 1] != static_cast<unsigned char>('\n'))
                    s_lineWidths[lines] += m_glyphSpacing.width();
                if (s_lineWidths[lines] > maxLineWidth)
                    maxLineWidth = s_lineWidths[lines];
                prevGlyph = g;
            }
        }
    }

    Point vpos(0, m_yOffset);
    lines = 0;
    int prevGlyph = -1;

    for (int i = 0; i < textLength; ++i) {
        const unsigned char g = p[i];

        if (g == static_cast<unsigned char>('\n') || i == 0) {
            if (g == static_cast<unsigned char>('\n')) {
                vpos.y += m_glyphHeight + m_glyphSpacing.height();
                ++lines;
                prevGlyph = -1;
            }
            if (align & Fw::AlignRight) {
                vpos.x = (maxLineWidth - (needLines ? s_lineWidths[lines] : 0));
            } else if (align & Fw::AlignHorizontalCenter) {
                vpos.x = (maxLineWidth - (needLines ? s_lineWidths[lines] : 0)) / 2;
            } else {
                vpos.x = 0;
            }
        }

        if (g >= 32 && g != static_cast<unsigned char>('\n')) {
            if (prevGlyph >= 0)
                vpos.x += m_kerning[prevGlyph][g];
            glyphsPositions[i] = vpos;
            vpos.x += advances[g] + m_glyphSpacing.width();
            prevGlyph = g;
        }
    }

    int minX = 0;
    int maxX = 0;
    bool hasGlyph = false;
    for (int i = 0; i < textLength; ++i) {
        const unsigned char g = p[i];
        if (g < 32 || g == static_cast<unsigned char>('\n'))
            continue;
        const int x0 = glyphsPositions[i].x + m_glyphsOffset[g].x;
        const int x1 = x0 + m_glyphsSize[g].width();
        if (!hasGlyph) {
            minX = x0;
            maxX = x1;
            hasGlyph = true;
        } else {
            minX = std::min(minX, x0);
            maxX = std::max(maxX, x1);
        }
    }

    if (hasGlyph && minX < 0) {
        for (int i = 0; i < textLength; ++i) {
            const unsigned char g = p[i];
            if (g >= 32 && g != static_cast<unsigned char>('\n'))
                glyphsPositions[i].x -= minX;
        }
        maxX -= minX;
        minX = 0;
    }

    if (textBoxSize) {
        const int actualWidth = hasGlyph ? std::max(maxLineWidth, maxX - minX) : maxLineWidth;
        textBoxSize->setWidth(actualWidth);
        textBoxSize->setHeight(vpos.y + m_glyphHeight);
    }
}

Size BitmapFont::calculateTextRectSize(const std::string_view text)
{
    Size size;
    calculateGlyphsPositions(text, Fw::AlignTopLeft, s_glyphsPositions, &size);
    return size;
}

void BitmapFont::calculateGlyphsWidthsAutomatically(const ImagePtr& image, const Size& glyphSize)
{
    if (!image)
        return;

    const auto& imageSize = image->getSize();
    const auto& texturePixels = image->getPixels();
    const int numHorizontalGlyphs = imageSize.width() / glyphSize.width();

    for (int glyph = m_firstGlyph; glyph < 256; ++glyph) {
        Rect glyphCoords(((glyph - m_firstGlyph) % numHorizontalGlyphs) * glyphSize.width(),
                         ((glyph - m_firstGlyph) / numHorizontalGlyphs) * glyphSize.height(),
                         glyphSize.width(),
                         m_glyphHeight);

        int width = glyphSize.width();
        for (int x = glyphCoords.left(); x <= glyphCoords.right(); ++x) {
            bool anyFilled = false;
            const int base = x * 4;
            for (int y = glyphCoords.top(); y <= glyphCoords.bottom(); ++y) {
                if (texturePixels[(y * imageSize.width() * 4) + base + 3] != 0) {
                    anyFilled = true;
                    break;
                }
            }
            if (anyFilled)
                width = x - glyphCoords.left() + 1;
        }
        m_glyphsSize[glyph].resize(width, m_glyphHeight);
    }
}

namespace {
    bool _isAscii(uint8_t c) { return c < 0x80; }
    bool _isSpace(uint8_t c) { return c == ' ' || c == '\t'; }
    bool _isHyphen(uint8_t c) { return c == '-'; }
    bool _utf8(const char* s, const char* e, uint32_t& cp, int& len) {
        if (s >= e)return false; unsigned char c0 = (unsigned char)s[0];
        if (c0 < 0x80) { cp = c0; len = 1; return true; }
        if ((c0 & 0xE0) == 0xC0 && s + 1 <= e) { cp = ((c0 & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F); len = 2; return true; }
        if ((c0 & 0xF0) == 0xE0 && s + 2 <= e) { cp = ((c0 & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F); len = 3; return true; }
        if ((c0 & 0xF8) == 0xF0 && s + 3 <= e) { cp = ((c0 & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) | (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F); len = 4; return true; }
        cp = c0; len = 1; return true;
    }
    bool _isCJK(uint32_t cp) {
        return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x3040 && cp <= 0x309F) || (cp >= 0x30A0 && cp <= 0x30FF) || (cp >= 0xAC00 && cp <= 0xD7AF);
    }
}

std::string BitmapFont::wrapText(std::string_view text, int maxWidth, const WrapOptions& options, std::vector<std::pair<int, Color>>* colors)
{
    if (text.empty() || maxWidth <= 0) return std::string(text);

    std::string out; out.reserve(text.size() + text.size() / 8);
    const char* cur = text.data(); const char* end = text.data() + text.size();
    const int sx = m_glyphSpacing.width(); const int maxW = std::max(0, maxWidth);
    uint8_t lastGlyph = 0xFF;

    auto glyphAdvanceWithKerning = [&](uint8_t prevGlyph, uint8_t glyph) -> int {
        int w = m_glyphsAdvance[glyph];
        if (prevGlyph != 0xFF)
            w += m_kerning[prevGlyph][glyph];
        return w;
    };

    int lineW = 0;
    int lastBreakOut = -1;
    int lastBreakLineW = 0;
    bool lastBreakHy = false;
    int lastBreakHyCount = 0;

    auto pushChar = [&](char ch) {
        out.push_back(ch);
        if (colors) updateColors(colors, (int)out.size() - 1, 1);
    };
    auto pushSlice = [&](const char* s, int n) {
        size_t o = out.size(); out.append(s, s + n);
        if (colors) updateColors(colors, (int)o, n);
    };
    auto newline = [&]() {
        out.push_back('\n');
        if (colors) updateColors(colors, (int)out.size() - 1, 1);
        lineW = 0; lastBreakOut = -1; lastBreakLineW = 0; lastBreakHy = false; lastBreakHyCount = 0;
        lastGlyph = 0xFF;
    };
    auto measure = [&](const char* s, int len, uint32_t cp)->int {
        if (len == 1 && cp < 256) return glyphAdvanceWithKerning(lastGlyph, static_cast<uint8_t>(cp)) + sx;
        return calculateTextRectSize(std::string_view(s, len)).width();
    };
    auto markBreak = [&](bool hy, int hyc) {
        lastBreakOut = (int)out.size(); lastBreakLineW = lineW; lastBreakHy = hy; lastBreakHyCount = hyc;
    };
    auto commitBreak = [&](bool forced) {
        if (lastBreakOut >= 0) {
            if (lastBreakHy && lastBreakHyCount > 0) {
                out.insert(out.begin() + lastBreakOut, '-');
                if (colors) updateColors(colors, lastBreakOut, 1);
                lastBreakOut += 1;
            }
            out.insert(out.begin() + lastBreakOut, '\n');
            if (colors) updateColors(colors, lastBreakOut, 1);
            lineW = std::max(0, lineW - lastBreakLineW);
        } else {
            if (forced && options.hyphenationMode == HyphenationMode::Auto) {
                out.push_back('-');
                if (colors) updateColors(colors, (int)out.size() - 1, 1);
            }
            newline();
            return;
        }
        lastBreakOut = -1; lastBreakLineW = 0; lastBreakHy = false; lastBreakHyCount = 0;
    };

    while (cur < end) {
        if (*cur == '\n') { pushChar('\n'); lineW = 0; lastBreakOut = -1; lastBreakLineW = 0; lastBreakHy = false; lastBreakHyCount = 0; lastGlyph = 0xFF; ++cur; continue; }

        uint32_t cp; int len; _utf8(cur, end, cp, len);

        if (cp == 0x00A0 && options.allowNoBreakSpace) {
            int w = measure(cur, len, cp);
            if (lineW + w > maxW) { commitBreak(true); }
            pushSlice(cur, len); lineW += w; cur += len;
            lastGlyph = static_cast<uint8_t>(cp);
            continue;
        }

        if (cp == 0x2060 && options.allowWordJoiner) {
            int w = measure(cur, len, cp);
            if (lineW + w > maxW) { commitBreak(true); }
            pushSlice(cur, len); lineW += w; cur += len;
            lastGlyph = cp < 256 ? static_cast<uint8_t>(cp) : 0xFF;
            continue;
        }

        if (cp == 0x200B && options.allowZeroWidthBreak) { markBreak(false, 0); cur += len; continue; }

        if (cp == 0x00AD && options.allowSoftHyphen) {
            bool show = (options.hyphenationMode == HyphenationMode::Manual || options.hyphenationMode == HyphenationMode::Auto);
            markBreak(show, show ? 1 : 0); cur += len; continue;
        }

        if (len == 1 && _isAscii((uint8_t)*cur)) {
            unsigned char ch = (unsigned char)*cur;
            if (_isSpace(ch)) {
                int w = glyphAdvanceWithKerning(lastGlyph, ' ') + sx;
                if (lineW + w > maxW) { commitBreak(false); ++cur; markBreak(false, 0); continue; }
                pushChar(' '); lineW += w; markBreak(false, 0); lastGlyph = ' '; ++cur; continue;
            }
            if (_isHyphen(ch)) {
                int w = glyphAdvanceWithKerning(lastGlyph, ch) + sx;
                if (lineW + w > maxW) commitBreak(false);
                pushChar((char)ch); lineW += w; markBreak(false, 0); lastGlyph = ch; ++cur; continue;
            }
            int w = glyphAdvanceWithKerning(lastGlyph, ch) + sx;
            if (lineW + w > maxW) {
                bool anywhere = (options.overflowWrapMode == OverflowWrapMode::Anywhere) || (options.wordBreakMode == WordBreakMode::BreakAll);
                if (lastBreakOut >= 0) commitBreak(false);
                else if (options.overflowWrapMode == OverflowWrapMode::BreakWord || anywhere) commitBreak(true);
                else newline();
            }
            pushChar((char)ch); lineW += w; lastGlyph = ch; ++cur; continue;
        }

        int w = measure(cur, len, cp);
        if (lineW + w > maxW) {
            bool anywhere = (options.overflowWrapMode == OverflowWrapMode::Anywhere) || (options.wordBreakMode == WordBreakMode::BreakAll) || (!options.keepCJKWordsTogether && _isCJK(cp));
            if (lastBreakOut >= 0) commitBreak(false);
            else if (options.overflowWrapMode == OverflowWrapMode::BreakWord || anywhere) commitBreak(true);
            else newline();
        }
        pushSlice(cur, len); lineW += w; cur += len;
        lastGlyph = cp < 256 ? static_cast<uint8_t>(cp) : 0xFF;
    }

    return out;
}

void BitmapFont::updateColors(std::vector<std::pair<int, Color>>* colors, const int pos, const int newTextLen) noexcept
{
    if (!colors) return;
    for (auto& it : *colors) {
        if (it.first > pos) {
            it.first += newTextLen;
        }
    }
}

const AtlasRegion* BitmapFont::getAtlasRegion() const noexcept {
    return m_texture ? m_texture->getAtlasRegion() : nullptr;
}

void BitmapFont::updateOutlineOffsets()
{
    m_outlineOffsets.clear();
    if (m_outlineThickness <= 0)
        return;

    const int radius = m_outlineThickness;
    const int radiusSquared = radius * radius;

    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x == 0 && y == 0)
                continue;
            if (x * x + y * y > radiusSquared)
                continue;
            m_outlineOffsets.emplace_back(x, y);
        }
    }

    if (m_outlineOffsets.empty())
        m_outlineThickness = 0;
}
