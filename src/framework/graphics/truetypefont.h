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

#pragma once

#include "declarations.h"
#include <array>
#include <cstdint>

struct TrueTypeFontSettings
{
    std::string file;
    float pixelSize{ 16.f };
    Size atlasSize{ 512, 512 };
    Size oversample{ 1, 1 };
    int firstGlyph{ 32 };
    int lastGlyph{ 255 };
    int padding{ 1 };
};

struct TrueTypeFontBuildResult
{
    ImagePtr atlasImage;
    std::array<Rect, 256> textureCoords{};
    std::array<Size, 256> glyphSize{};
    std::array<Point, 256> glyphOffset{};
    std::array<int, 256> glyphAdvance{};
    int glyphHeight{ 0 };
    int baseline{ 0 };
    std::array<std::array<int16_t, 256>, 256> kerning{};
};

class TrueTypeFontBuilder
{
public:
    TrueTypeFontBuildResult build(const TrueTypeFontSettings& settings) const;
};

