/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-11 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

#ifndef __JUCE_RENDERINGHELPERS_JUCEHEADER__
#define __JUCE_RENDERINGHELPERS_JUCEHEADER__

#if JUCE_MSVC
 #pragma warning (push)
 #pragma warning (disable: 4127) // "expression is constant" warning
#endif

namespace RenderingHelpers
{

//==============================================================================
/** Holds either a simple integer translation, or an affine transform.
*/
class TranslationOrTransform
{
public:
    TranslationOrTransform (int x, int y) noexcept
        : xOffset (x), yOffset (y), isOnlyTranslated (true), isIntegerScaling (true)
    {
    }

    TranslationOrTransform (const TranslationOrTransform& other) noexcept
        : complexTransform (other.complexTransform),
          xOffset (other.xOffset), yOffset (other.yOffset),
          isOnlyTranslated (other.isOnlyTranslated), isIntegerScaling (other.isIntegerScaling)
    {
    }

    AffineTransform getTransform() const noexcept
    {
        return isOnlyTranslated ? AffineTransform::translation ((float) xOffset, (float) yOffset)
                                : complexTransform;
    }

    AffineTransform getTransformWith (const AffineTransform& userTransform) const noexcept
    {
        return isOnlyTranslated ? userTransform.translated ((float) xOffset, (float) yOffset)
                                : userTransform.followedBy (complexTransform);
    }

    void setOrigin (const int x, const int y) noexcept
    {
        if (isOnlyTranslated)
        {
            xOffset += x;
            yOffset += y;
        }
        else
        {
            complexTransform = AffineTransform::translation ((float) x, (float) y)
                                               .followedBy (complexTransform);
        }
    }

    void addTransform (const AffineTransform& t) noexcept
    {
        if (isOnlyTranslated
             && t.isOnlyTranslation()
             && isIntegerTranslation (t))
        {
            xOffset += (int) t.getTranslationX();
            yOffset += (int) t.getTranslationY();
        }
        else
        {
            complexTransform = getTransformWith (t);
            isOnlyTranslated = false;
            isIntegerScaling = isIntegerScale (complexTransform);
        }
    }

    float getScaleFactor() const noexcept
    {
        return isOnlyTranslated ? 1.0f : complexTransform.getScaleFactor();
    }

    void moveOriginInDeviceSpace (const int dx, const int dy) noexcept
    {
        if (isOnlyTranslated)
        {
            xOffset += dx;
            yOffset += dy;
        }
        else
        {
            complexTransform = complexTransform.translated ((float) dx, (float) dy);
        }
    }

    template <typename Type>
    Rectangle<Type> translated (const Rectangle<Type>& r) const noexcept
    {
        jassert (isOnlyTranslated);
        return r.translated (static_cast <Type> (xOffset),
                             static_cast <Type> (yOffset));
    }

    template <typename Type>
    Rectangle<float> transformed (const Rectangle<Type>& r) const noexcept
    {
        return r.toFloat().transformed (complexTransform);
    }

    Rectangle<int> deviceSpaceToUserSpace (const Rectangle<int>& r) const noexcept
    {
        return isOnlyTranslated ? r.translated (-xOffset, -yOffset)
                                : r.toFloat().transformed (complexTransform.inverted()).getSmallestIntegerContainer();
    }

    AffineTransform complexTransform;
    int xOffset, yOffset;
    bool isOnlyTranslated, isIntegerScaling;

private:
    static inline bool isIntegerTranslation (const AffineTransform& t) noexcept
    {
        const int tx = (int) (t.getTranslationX() * 256.0f);
        const int ty = (int) (t.getTranslationY() * 256.0f);
        return ((tx | ty) & 0xf8) == 0;
    }

    static inline bool isIntegerScale (const AffineTransform& t) noexcept
    {
        if (t.mat01 != 0 || t.mat10 != 0)
            return false;

        const int tx = (int) (t.getTranslationX() * 256.0f);
        const int ty = (int) (t.getTranslationY() * 256.0f);
        const int txs = (int) (t.mat00 * 256.0f);
        const int tys = (int) (t.mat11 * 256.0f);

        return ((tx | ty | txs | tys) & 0xf8) == 0;
    }
};

//==============================================================================
/** Holds a cache of recently-used glyph objects of some type. */
template <class CachedGlyphType, class RenderTargetType>
class GlyphCache  : private DeletedAtShutdown
{
public:
    GlyphCache()
    {
        addNewGlyphSlots (120);
    }

    ~GlyphCache()
    {
        getSingletonPointer() = nullptr;
    }

    static GlyphCache& getInstance()
    {
        GlyphCache*& g = getSingletonPointer();

        if (g == nullptr)
            g = new GlyphCache();

        return *g;
    }

    //==============================================================================
    void drawGlyph (RenderTargetType& target, const Font& font, const int glyphNumber, float x, float y)
    {
        ++accessCounter;
        CachedGlyphType* glyph = nullptr;

        const ScopedReadLock srl (lock);

        for (int i = glyphs.size(); --i >= 0;)
        {
            CachedGlyphType* const g = glyphs.getUnchecked (i);

            if (g->glyph == glyphNumber && g->font == font)
            {
                glyph = g;
                ++hits;
                break;
            }
        }

        if (glyph == nullptr)
        {
            ++misses;
            const ScopedWriteLock swl (lock);

            if (hits.value + misses.value > glyphs.size() * 16)
            {
                if (misses.value * 2 > hits.value)
                    addNewGlyphSlots (32);

                hits.set (0);
                misses.set (0);
                glyph = glyphs.getLast();
            }
            else
            {
                glyph = findLeastRecentlyUsedGlyph();
            }

            jassert (glyph != nullptr);
            glyph->generate (font, glyphNumber);
        }

        glyph->lastAccessCount = accessCounter.value;
        glyph->draw (target, x, y);
    }

private:
    friend class OwnedArray <CachedGlyphType>;
    OwnedArray <CachedGlyphType> glyphs;
    Atomic<int> accessCounter, hits, misses;
    ReadWriteLock lock;

    void addNewGlyphSlots (int num)
    {
        while (--num >= 0)
            glyphs.add (new CachedGlyphType());
    }

    CachedGlyphType* findLeastRecentlyUsedGlyph() const noexcept
    {
        CachedGlyphType* oldest = glyphs.getLast();
        int oldestCounter = oldest->lastAccessCount;

        for (int i = glyphs.size() - 1; --i >= 0;)
        {
            CachedGlyphType* const glyph = glyphs.getUnchecked(i);

            if (glyph->lastAccessCount <= oldestCounter)
            {
                oldestCounter = glyph->lastAccessCount;
                oldest = glyph;
            }
        }

        return oldest;
    }

    static GlyphCache*& getSingletonPointer() noexcept
    {
        static GlyphCache* g = nullptr;
        return g;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlyphCache)
};

//==============================================================================
/** Caches a glyph as an edge-table. */
template <class RendererType>
class CachedGlyphEdgeTable
{
public:
    CachedGlyphEdgeTable() : glyph (0), lastAccessCount (0) {}

    void draw (RendererType& state, float x, const float y) const
    {
        if (snapToIntegerCoordinate)
            x = std::floor (x + 0.5f);

        if (edgeTable != nullptr)
            state.fillEdgeTable (*edgeTable, x, roundToInt (y));
    }

    void generate (const Font& newFont, const int glyphNumber)
    {
        font = newFont;
        Typeface* const typeface = newFont.getTypeface();
        snapToIntegerCoordinate = typeface->isHinted();
        glyph = glyphNumber;

        const float fontHeight = font.getHeight();
        edgeTable = typeface->getEdgeTableForGlyph (glyphNumber,
                                                    AffineTransform::scale (fontHeight * font.getHorizontalScale(), fontHeight)
                                                                  #if JUCE_MAC || JUCE_IOS
                                                                    .translated (0.0f, -0.5f)
                                                                  #endif
                                                    );
    }

    Font font;
    int glyph, lastAccessCount;
    bool snapToIntegerCoordinate;

private:
    ScopedPointer <EdgeTable> edgeTable;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CachedGlyphEdgeTable)
};

//==============================================================================
/** Calculates the alpha values and positions for rendering the edges of a
    non-pixel-aligned rectangle.
*/
struct FloatRectangleRasterisingInfo
{
    FloatRectangleRasterisingInfo (const Rectangle<float>& area)
        : left   (roundToInt (256.0f * area.getX())),
          top    (roundToInt (256.0f * area.getY())),
          right  (roundToInt (256.0f * area.getRight())),
          bottom (roundToInt (256.0f * area.getBottom()))
    {
        if ((top >> 8) == (bottom >> 8))
        {
            topAlpha = bottom - top;
            bottomAlpha = 0;
            totalTop = top >> 8;
            totalBottom = bottom = top = totalTop + 1;
        }
        else
        {
            if ((top & 255) == 0)
            {
                topAlpha = 0;
                top = totalTop = (top >> 8);
            }
            else
            {
                topAlpha = 255 - (top & 255);
                totalTop = (top >> 8);
                top = totalTop + 1;
            }

            bottomAlpha = bottom & 255;
            bottom >>= 8;
            totalBottom = bottom + (bottomAlpha != 0 ? 1 : 0);
        }

        if ((left >> 8) == (right >> 8))
        {
            leftAlpha = right - left;
            rightAlpha = 0;
            totalLeft = (left >> 8);
            totalRight = right = left = totalLeft + 1;
        }
        else
        {
            if ((left & 255) == 0)
            {
                leftAlpha = 0;
                left = totalLeft = (left >> 8);
            }
            else
            {
                leftAlpha = 255 - (left & 255);
                totalLeft = (left >> 8);
                left = totalLeft + 1;
            }

            rightAlpha = right & 255;
            right >>= 8;
            totalRight = right + (rightAlpha != 0 ? 1 : 0);
        }
    }

    template <class Callback>
    void iterate (Callback& callback) const
    {
        if (topAlpha != 0)       callback (totalLeft, totalTop, totalRight - totalLeft, 1, topAlpha);
        if (bottomAlpha != 0)    callback (totalLeft, bottom,   totalRight - totalLeft, 1, bottomAlpha);
        if (leftAlpha != 0)      callback (totalLeft, totalTop, 1, totalBottom - totalTop, leftAlpha);
        if (rightAlpha != 0)     callback (right,     totalTop, 1, totalBottom - totalTop, rightAlpha);

        callback (left, top, right - left, bottom - top, 255);
    }

    inline bool isOnePixelWide() const noexcept            { return right - left == 1 && leftAlpha + rightAlpha == 0; }

    inline int getTopLeftCornerAlpha() const noexcept      { return (topAlpha * leftAlpha) >> 8; }
    inline int getTopRightCornerAlpha() const noexcept     { return (topAlpha * rightAlpha) >> 8; }
    inline int getBottomLeftCornerAlpha() const noexcept   { return (bottomAlpha * leftAlpha) >> 8; }
    inline int getBottomRightCornerAlpha() const noexcept  { return (bottomAlpha * rightAlpha) >> 8; }

    //==============================================================================
    int left, top, right, bottom;  // bounds of the solid central area, excluding anti-aliased edges
    int totalTop, totalLeft, totalBottom, totalRight; // bounds of the total area, including edges
    int topAlpha, leftAlpha, bottomAlpha, rightAlpha; // alpha of each anti-aliased edge
};

//==============================================================================
/** Contains classes for calculating the colour of pixels within various types of gradient. */
namespace GradientPixelIterators
{
    /** Iterates the colour of pixels in a linear gradient */
    class Linear
    {
    public:
        Linear (const ColourGradient& gradient, const AffineTransform& transform,
                const PixelARGB* const colours, const int numColours)
            : lookupTable (colours),
              numEntries (numColours)
        {
            jassert (numColours >= 0);
            Point<float> p1 (gradient.point1);
            Point<float> p2 (gradient.point2);

            if (! transform.isIdentity())
            {
                const Line<float> l (p2, p1);
                Point<float> p3 = l.getPointAlongLine (0.0f, 100.0f);

                p1.applyTransform (transform);
                p2.applyTransform (transform);
                p3.applyTransform (transform);

                p2 = Line<float> (p2, p3).findNearestPointTo (p1);
            }

            vertical   = std::abs (p1.x - p2.x) < 0.001f;
            horizontal = std::abs (p1.y - p2.y) < 0.001f;

            if (vertical)
            {
                scale = roundToInt ((numEntries << (int) numScaleBits) / (double) (p2.y - p1.y));
                start = roundToInt (p1.y * scale);
            }
            else if (horizontal)
            {
                scale = roundToInt ((numEntries << (int) numScaleBits) / (double) (p2.x - p1.x));
                start = roundToInt (p1.x * scale);
            }
            else
            {
                grad = (p2.getY() - p1.y) / (double) (p1.x - p2.x);
                yTerm = p1.getY() - p1.x / grad;
                scale = roundToInt ((numEntries << (int) numScaleBits) / (yTerm * grad - (p2.y * grad - p2.x)));
                grad *= scale;
            }
        }

        forcedinline void setY (const int y) noexcept
        {
            if (vertical)
                linePix = lookupTable [jlimit (0, numEntries, (y * scale - start) >> (int) numScaleBits)];
            else if (! horizontal)
                start = roundToInt ((y - yTerm) * grad);
        }

        inline PixelARGB getPixel (const int x) const noexcept
        {
            return vertical ? linePix
                            : lookupTable [jlimit (0, numEntries, (x * scale - start) >> (int) numScaleBits)];
        }

    private:
        const PixelARGB* const lookupTable;
        const int numEntries;
        PixelARGB linePix;
        int start, scale;
        double grad, yTerm;
        bool vertical, horizontal;
        enum { numScaleBits = 12 };

        JUCE_DECLARE_NON_COPYABLE (Linear)
    };

    //==============================================================================
    /** Iterates the colour of pixels in a circular radial gradient */
    class Radial
    {
    public:
        Radial (const ColourGradient& gradient, const AffineTransform&,
                const PixelARGB* const colours, const int numColours)
            : lookupTable (colours),
              numEntries (numColours),
              gx1 (gradient.point1.x),
              gy1 (gradient.point1.y)
        {
            jassert (numColours >= 0);
            const Point<float> diff (gradient.point1 - gradient.point2);
            maxDist = diff.x * diff.x + diff.y * diff.y;
            invScale = numEntries / std::sqrt (maxDist);
            jassert (roundToInt (std::sqrt (maxDist) * invScale) <= numEntries);
        }

        forcedinline void setY (const int y) noexcept
        {
            dy = y - gy1;
            dy *= dy;
        }

        inline PixelARGB getPixel (const int px) const noexcept
        {
            double x = px - gx1;
            x *= x;
            x += dy;

            return lookupTable [x >= maxDist ? numEntries : roundToInt (std::sqrt (x) * invScale)];
        }

    protected:
        const PixelARGB* const lookupTable;
        const int numEntries;
        const double gx1, gy1;
        double maxDist, invScale, dy;

        JUCE_DECLARE_NON_COPYABLE (Radial)
    };

    //==============================================================================
    /** Iterates the colour of pixels in a skewed radial gradient */
    class TransformedRadial   : public Radial
    {
    public:
        TransformedRadial (const ColourGradient& gradient, const AffineTransform& transform,
                           const PixelARGB* const colours, const int numColours)
            : Radial (gradient, transform, colours, numColours),
              inverseTransform (transform.inverted())
        {
            tM10 = inverseTransform.mat10;
            tM00 = inverseTransform.mat00;
        }

        forcedinline void setY (const int y) noexcept
        {
            lineYM01 = inverseTransform.mat01 * y + inverseTransform.mat02 - gx1;
            lineYM11 = inverseTransform.mat11 * y + inverseTransform.mat12 - gy1;
        }

        inline PixelARGB getPixel (const int px) const noexcept
        {
            double x = px;
            const double y = tM10 * x + lineYM11;
            x = tM00 * x + lineYM01;
            x *= x;
            x += y * y;

            if (x >= maxDist)
                return lookupTable [numEntries];

            return lookupTable [jmin (numEntries, roundToInt (std::sqrt (x) * invScale))];
        }

    private:
        double tM10, tM00, lineYM01, lineYM11;
        const AffineTransform inverseTransform;

        JUCE_DECLARE_NON_COPYABLE (TransformedRadial)
    };
}

#define JUCE_PERFORM_PIXEL_OP_LOOP(op) \
{ \
    const int destStride = destData.pixelStride;  \
    do { dest->op; dest = addBytesToPointer (dest, destStride); } while (--width > 0); \
}

//==============================================================================
/** Contains classes for filling edge tables with various fill types. */
namespace EdgeTableFillers
{
    /** Fills an edge-table with a solid colour. */
    template <class PixelType, bool replaceExisting = false>
    class SolidColour
    {
    public:
        SolidColour (const Image::BitmapData& image, const PixelARGB& colour)
            : destData (image), sourceColour (colour)
        {
            if (sizeof (PixelType) == 3 && destData.pixelStride == sizeof (PixelType))
            {
                areRGBComponentsEqual = sourceColour.getRed() == sourceColour.getGreen()
                                            && sourceColour.getGreen() == sourceColour.getBlue();
                filler[0].set (sourceColour);
                filler[1].set (sourceColour);
                filler[2].set (sourceColour);
                filler[3].set (sourceColour);
            }
        }

        forcedinline void setEdgeTableYPos (const int y) noexcept
        {
            linePixels = (PixelType*) destData.getLinePointer (y);
        }

        forcedinline void handleEdgeTablePixel (const int x, const int alphaLevel) const noexcept
        {
            if (replaceExisting)
                getPixel (x)->set (sourceColour);
            else
                getPixel (x)->blend (sourceColour, (uint32) alphaLevel);
        }

        forcedinline void handleEdgeTablePixelFull (const int x) const noexcept
        {
            if (replaceExisting)
                getPixel (x)->set (sourceColour);
            else
                getPixel (x)->blend (sourceColour);
        }

        forcedinline void handleEdgeTableLine (const int x, const int width, const int alphaLevel) const noexcept
        {
            PixelARGB p (sourceColour);
            p.multiplyAlpha (alphaLevel);

            PixelType* dest = getPixel (x);

            if (replaceExisting || p.getAlpha() >= 0xff)
                replaceLine (dest, p, width);
            else
                blendLine (dest, p, width);
        }

        forcedinline void handleEdgeTableLineFull (const int x, const int width) const noexcept
        {
            PixelType* dest = getPixel (x);

            if (replaceExisting || sourceColour.getAlpha() >= 0xff)
                replaceLine (dest, sourceColour, width);
            else
                blendLine (dest, sourceColour, width);
        }

    private:
        const Image::BitmapData& destData;
        PixelType* linePixels;
        PixelARGB sourceColour;
        PixelRGB filler [4];
        bool areRGBComponentsEqual;

        forcedinline PixelType* getPixel (const int x) const noexcept
        {
            return addBytesToPointer (linePixels, x * destData.pixelStride);
        }

        inline void blendLine (PixelType* dest, const PixelARGB& colour, int width) const noexcept
        {
            JUCE_PERFORM_PIXEL_OP_LOOP (blend (colour))
        }

        forcedinline void replaceLine (PixelRGB* dest, const PixelARGB& colour, int width) const noexcept
        {
            if (destData.pixelStride == sizeof (*dest))
            {
                if (areRGBComponentsEqual)  // if all the component values are the same, we can cheat..
                {
                    memset (dest, colour.getRed(), (size_t) width * 3);
                }
                else
                {
                    if (width >> 5)
                    {
                        const int* const intFiller = reinterpret_cast<const int*> (filler);

                        while (width > 8 && (((pointer_sized_int) dest) & 7) != 0)
                        {
                            dest->set (colour);
                            ++dest;
                            --width;
                        }

                        while (width > 4)
                        {
                            int* d = reinterpret_cast<int*> (dest);
                            *d++ = intFiller[0];
                            *d++ = intFiller[1];
                            *d++ = intFiller[2];
                            dest = reinterpret_cast<PixelRGB*> (d);
                            width -= 4;
                        }
                    }

                    while (--width >= 0)
                    {
                        dest->set (colour);
                        ++dest;
                    }
                }
            }
            else
            {
                JUCE_PERFORM_PIXEL_OP_LOOP (set (colour))
            }
        }

        forcedinline void replaceLine (PixelAlpha* dest, const PixelARGB& colour, int width) const noexcept
        {
            if (destData.pixelStride == sizeof (*dest))
                memset (dest, colour.getAlpha(), (size_t) width);
            else
                JUCE_PERFORM_PIXEL_OP_LOOP (setAlpha (colour.getAlpha()))
        }

        forcedinline void replaceLine (PixelARGB* dest, const PixelARGB& colour, int width) const noexcept
        {
            JUCE_PERFORM_PIXEL_OP_LOOP (set (colour))
        }

        JUCE_DECLARE_NON_COPYABLE (SolidColour)
    };

    //==============================================================================
    /** Fills an edge-table with a gradient. */
    template <class PixelType, class GradientType>
    class Gradient  : public GradientType
    {
    public:
        Gradient (const Image::BitmapData& dest, const ColourGradient& gradient, const AffineTransform& transform,
                  const PixelARGB* const colours, const int numColours)
            : GradientType (gradient, transform, colours, numColours - 1),
              destData (dest)
        {
        }

        forcedinline void setEdgeTableYPos (const int y) noexcept
        {
            linePixels = (PixelType*) destData.getLinePointer (y);
            GradientType::setY (y);
        }

        forcedinline void handleEdgeTablePixel (const int x, const int alphaLevel) const noexcept
        {
            getPixel (x)->blend (GradientType::getPixel (x), (uint32) alphaLevel);
        }

        forcedinline void handleEdgeTablePixelFull (const int x) const noexcept
        {
            getPixel (x)->blend (GradientType::getPixel (x));
        }

        void handleEdgeTableLine (int x, int width, const int alphaLevel) const noexcept
        {
            PixelType* dest = getPixel (x);

            if (alphaLevel < 0xff)
                JUCE_PERFORM_PIXEL_OP_LOOP (blend (GradientType::getPixel (x++), (uint32) alphaLevel))
            else
                JUCE_PERFORM_PIXEL_OP_LOOP (blend (GradientType::getPixel (x++)))
        }

        void handleEdgeTableLineFull (int x, int width) const noexcept
        {
            PixelType* dest = getPixel (x);
            JUCE_PERFORM_PIXEL_OP_LOOP (blend (GradientType::getPixel (x++)))
        }

    private:
        const Image::BitmapData& destData;
        PixelType* linePixels;

        forcedinline PixelType* getPixel (const int x) const noexcept
        {
            return addBytesToPointer (linePixels, x * destData.pixelStride);
        }

        JUCE_DECLARE_NON_COPYABLE (Gradient)
    };

    //==============================================================================
    /** Fills an edge-table with a non-transformed image. */
    template <class DestPixelType, class SrcPixelType, bool repeatPattern>
    class ImageFill
    {
    public:
        ImageFill (const Image::BitmapData& dest, const Image::BitmapData& src,
                   const int alpha, const int x, const int y)
            : destData (dest),
              srcData (src),
              extraAlpha (alpha + 1),
              xOffset (repeatPattern ? negativeAwareModulo (x, src.width)  - src.width  : x),
              yOffset (repeatPattern ? negativeAwareModulo (y, src.height) - src.height : y)
        {
        }

        forcedinline void setEdgeTableYPos (int y) noexcept
        {
            linePixels = (DestPixelType*) destData.getLinePointer (y);

            y -= yOffset;
            if (repeatPattern)
            {
                jassert (y >= 0);
                y %= srcData.height;
            }

            sourceLineStart = (SrcPixelType*) srcData.getLinePointer (y);
        }

        forcedinline void handleEdgeTablePixel (const int x, int alphaLevel) const noexcept
        {
            alphaLevel = (alphaLevel * extraAlpha) >> 8;

            getDestPixel (x)->blend (*getSrcPixel (repeatPattern ? ((x - xOffset) % srcData.width) : (x - xOffset)), (uint32) alphaLevel);
        }

        forcedinline void handleEdgeTablePixelFull (const int x) const noexcept
        {
            getDestPixel (x)->blend (*getSrcPixel (repeatPattern ? ((x - xOffset) % srcData.width) : (x - xOffset)), (uint32) extraAlpha);
        }

        void handleEdgeTableLine (int x, int width, int alphaLevel) const noexcept
        {
            DestPixelType* dest = getDestPixel (x);
            alphaLevel = (alphaLevel * extraAlpha) >> 8;
            x -= xOffset;

            jassert (repeatPattern || (x >= 0 && x + width <= srcData.width));

            if (alphaLevel < 0xfe)
            {
                JUCE_PERFORM_PIXEL_OP_LOOP (blend (*getSrcPixel (repeatPattern ? (x++ % srcData.width) : x++), (uint32) alphaLevel))
            }
            else
            {
                if (repeatPattern)
                    JUCE_PERFORM_PIXEL_OP_LOOP (blend (*getSrcPixel (x++ % srcData.width)))
                else
                    copyRow (dest, getSrcPixel (x), width);
            }
        }

        void handleEdgeTableLineFull (int x, int width) const noexcept
        {
            DestPixelType* dest = getDestPixel (x);
            x -= xOffset;

            jassert (repeatPattern || (x >= 0 && x + width <= srcData.width));

            if (extraAlpha < 0xfe)
            {
                JUCE_PERFORM_PIXEL_OP_LOOP (blend (*getSrcPixel (repeatPattern ? (x++ % srcData.width) : x++), (uint32) extraAlpha))
            }
            else
            {
                if (repeatPattern)
                    JUCE_PERFORM_PIXEL_OP_LOOP (blend (*getSrcPixel (x++ % srcData.width)))
                else
                    copyRow (dest, getSrcPixel (x), width);
            }
        }

        void clipEdgeTableLine (EdgeTable& et, int x, int y, int width)
        {
            jassert (x - xOffset >= 0 && x + width - xOffset <= srcData.width);
            SrcPixelType* s = (SrcPixelType*) srcData.getLinePointer (y - yOffset);
            uint8* mask = (uint8*) (s + x - xOffset);

            if (sizeof (SrcPixelType) == sizeof (PixelARGB))
                mask += PixelARGB::indexA;

            et.clipLineToMask (x, y, mask, sizeof (SrcPixelType), width);
        }

    private:
        const Image::BitmapData& destData;
        const Image::BitmapData& srcData;
        const int extraAlpha, xOffset, yOffset;
        DestPixelType* linePixels;
        SrcPixelType* sourceLineStart;

        forcedinline DestPixelType* getDestPixel (int const x) const noexcept
        {
            return addBytesToPointer (linePixels, x * destData.pixelStride);
        }

        forcedinline SrcPixelType const* getSrcPixel (int const x) const noexcept
        {
            return addBytesToPointer (sourceLineStart, x * srcData.pixelStride);
        }

        forcedinline void copyRow (DestPixelType* dest, SrcPixelType const* src, int width) const noexcept
        {
            if (srcData.pixelStride == 3 && destData.pixelStride == 3)
            {
                memcpy (dest, src, sizeof (PixelRGB) * (size_t) width);
            }
            else
            {
                const int destStride = destData.pixelStride;
                const int srcStride = srcData.pixelStride;

                do
                {
                    dest->blend (*src);
                    dest = addBytesToPointer (dest, destStride);
                    src = addBytesToPointer (src, srcStride);
                } while (--width > 0);
            }
        }

        JUCE_DECLARE_NON_COPYABLE (ImageFill)
    };

    //==============================================================================
    /** Fills an edge-table with a transformed image. */
    template <class DestPixelType, class SrcPixelType, bool repeatPattern>
    class TransformedImageFill
    {
    public:
        TransformedImageFill (const Image::BitmapData& dest, const Image::BitmapData& src,
                              const AffineTransform& transform, const int alpha, const bool higherQuality)
            : interpolator (transform,
                            higherQuality ? 0.5f : 0.0f,
                            higherQuality ? -128 : 0),
              destData (dest),
              srcData (src),
              extraAlpha (alpha + 1),
              betterQuality (higherQuality),
              maxX (src.width  - 1),
              maxY (src.height - 1),
              scratchSize (2048)
        {
            scratchBuffer.malloc (scratchSize);
        }

        forcedinline void setEdgeTableYPos (const int newY) noexcept
        {
            y = newY;
            linePixels = (DestPixelType*) destData.getLinePointer (newY);
        }

        forcedinline void handleEdgeTablePixel (const int x, const int alphaLevel) noexcept
        {
            SrcPixelType p;
            generate (&p, x, 1);

            getDestPixel (x)->blend (p, (uint32) (alphaLevel * extraAlpha) >> 8);
        }

        forcedinline void handleEdgeTablePixelFull (const int x) noexcept
        {
            SrcPixelType p;
            generate (&p, x, 1);

            getDestPixel (x)->blend (p, (uint32) extraAlpha);
        }

        void handleEdgeTableLine (const int x, int width, int alphaLevel) noexcept
        {
            if (width > (int) scratchSize)
            {
                scratchSize = (size_t) width;
                scratchBuffer.malloc (scratchSize);
            }

            SrcPixelType* span = scratchBuffer;
            generate (span, x, width);

            DestPixelType* dest = getDestPixel (x);
            alphaLevel *= extraAlpha;
            alphaLevel >>= 8;

            if (alphaLevel < 0xfe)
                JUCE_PERFORM_PIXEL_OP_LOOP (blend (*span++, (uint32) alphaLevel))
            else
                JUCE_PERFORM_PIXEL_OP_LOOP (blend (*span++))
        }

        forcedinline void handleEdgeTableLineFull (const int x, int width) noexcept
        {
            handleEdgeTableLine (x, width, 255);
        }

        void clipEdgeTableLine (EdgeTable& et, int x, int y_, int width)
        {
            if (width > (int) scratchSize)
            {
                scratchSize = (size_t) width;
                scratchBuffer.malloc (scratchSize);
            }

            y = y_;
            generate (scratchBuffer.getData(), x, width);

            et.clipLineToMask (x, y_,
                               reinterpret_cast<uint8*> (scratchBuffer.getData()) + SrcPixelType::indexA,
                               sizeof (SrcPixelType), width);
        }

    private:
        forcedinline DestPixelType* getDestPixel (const int x) const noexcept
        {
            return addBytesToPointer (linePixels, x * destData.pixelStride);
        }

        //==============================================================================
        template <class PixelType>
        void generate (PixelType* dest, const int x, int numPixels) noexcept
        {
            this->interpolator.setStartOfLine ((float) x, (float) y, numPixels);

            do
            {
                int hiResX, hiResY;
                this->interpolator.next (hiResX, hiResY);

                int loResX = hiResX >> 8;
                int loResY = hiResY >> 8;

                if (repeatPattern)
                {
                    loResX = negativeAwareModulo (loResX, srcData.width);
                    loResY = negativeAwareModulo (loResY, srcData.height);
                }

                if (betterQuality)
                {
                    if (isPositiveAndBelow (loResX, maxX))
                    {
                        if (isPositiveAndBelow (loResY, maxY))
                        {
                            // In the centre of the image..
                            render4PixelAverage (dest, this->srcData.getPixelPointer (loResX, loResY),
                                                 hiResX & 255, hiResY & 255);
                            ++dest;
                            continue;
                        }
                        else if (! repeatPattern)
                        {
                            // At a top or bottom edge..
                            if (loResY < 0)
                                render2PixelAverageX (dest, this->srcData.getPixelPointer (loResX, 0), hiResX & 255);
                            else
                                render2PixelAverageX (dest, this->srcData.getPixelPointer (loResX, maxY), hiResX & 255);

                            ++dest;
                            continue;
                        }
                    }
                    else
                    {
                        if (isPositiveAndBelow (loResY, maxY) && ! repeatPattern)
                        {
                            // At a left or right hand edge..
                            if (loResX < 0)
                                render2PixelAverageY (dest, this->srcData.getPixelPointer (0, loResY), hiResY & 255);
                            else
                                render2PixelAverageY (dest, this->srcData.getPixelPointer (maxX, loResY), hiResY & 255);

                            ++dest;
                            continue;
                        }
                    }
                }

                if (! repeatPattern)
                {
                    if (loResX < 0)     loResX = 0;
                    if (loResY < 0)     loResY = 0;
                    if (loResX > maxX)  loResX = maxX;
                    if (loResY > maxY)  loResY = maxY;
                }

                dest->set (*(const PixelType*) this->srcData.getPixelPointer (loResX, loResY));
                ++dest;

            } while (--numPixels > 0);
        }

        //==============================================================================
        void render4PixelAverage (PixelARGB* const dest, const uint8* src, const int subPixelX, const int subPixelY) noexcept
        {
            uint32 c[4] = { 256 * 128, 256 * 128, 256 * 128, 256 * 128 };

            uint32 weight = (uint32) ((256 - subPixelX) * (256 - subPixelY));
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];
            c[3] += weight * src[3];

            src += this->srcData.pixelStride;

            weight = (uint32) (subPixelX * (256 - subPixelY));
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];
            c[3] += weight * src[3];

            src += this->srcData.lineStride;

            weight = (uint32) (subPixelX * subPixelY);
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];
            c[3] += weight * src[3];

            src -= this->srcData.pixelStride;

            weight = (uint32) ((256 - subPixelX) * subPixelY);
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];
            c[3] += weight * src[3];

            dest->setARGB ((uint8) (c[PixelARGB::indexA] >> 16),
                           (uint8) (c[PixelARGB::indexR] >> 16),
                           (uint8) (c[PixelARGB::indexG] >> 16),
                           (uint8) (c[PixelARGB::indexB] >> 16));
        }

        void render2PixelAverageX (PixelARGB* const dest, const uint8* src, const uint32 subPixelX) noexcept
        {
            uint32 c[4] = { 128, 128, 128, 128 };

            uint32 weight = 256 - subPixelX;
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];
            c[3] += weight * src[3];

            src += this->srcData.pixelStride;

            weight = subPixelX;
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];
            c[3] += weight * src[3];

            dest->setARGB ((uint8) (c[PixelARGB::indexA] >> 8),
                           (uint8) (c[PixelARGB::indexR] >> 8),
                           (uint8) (c[PixelARGB::indexG] >> 8),
                           (uint8) (c[PixelARGB::indexB] >> 8));
        }

        void render2PixelAverageY (PixelARGB* const dest, const uint8* src, const uint32 subPixelY) noexcept
        {
            uint32 c[4] = { 128, 128, 128, 128 };

            uint32 weight = 256 - subPixelY;
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];
            c[3] += weight * src[3];

            src += this->srcData.lineStride;

            weight = subPixelY;
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];
            c[3] += weight * src[3];

            dest->setARGB ((uint8) (c[PixelARGB::indexA] >> 8),
                           (uint8) (c[PixelARGB::indexR] >> 8),
                           (uint8) (c[PixelARGB::indexG] >> 8),
                           (uint8) (c[PixelARGB::indexB] >> 8));
        }

        //==============================================================================
        void render4PixelAverage (PixelRGB* const dest, const uint8* src, const uint32 subPixelX, const uint32 subPixelY) noexcept
        {
            uint32 c[3] = { 256 * 128, 256 * 128, 256 * 128 };

            uint32 weight = (256 - subPixelX) * (256 - subPixelY);
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];

            src += this->srcData.pixelStride;

            weight = subPixelX * (256 - subPixelY);
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];

            src += this->srcData.lineStride;

            weight = subPixelX * subPixelY;
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];

            src -= this->srcData.pixelStride;

            weight = (256 - subPixelX) * subPixelY;
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];

            dest->setARGB ((uint8) 255,
                           (uint8) (c[PixelRGB::indexR] >> 16),
                           (uint8) (c[PixelRGB::indexG] >> 16),
                           (uint8) (c[PixelRGB::indexB] >> 16));
        }

        void render2PixelAverageX (PixelRGB* const dest, const uint8* src, const uint32 subPixelX) noexcept
        {
            uint32 c[3] = { 128, 128, 128 };

            const uint32 weight = 256 - subPixelX;
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];

            src += this->srcData.pixelStride;

            c[0] += subPixelX * src[0];
            c[1] += subPixelX * src[1];
            c[2] += subPixelX * src[2];

            dest->setARGB ((uint8) 255,
                           (uint8) (c[PixelRGB::indexR] >> 8),
                           (uint8) (c[PixelRGB::indexG] >> 8),
                           (uint8) (c[PixelRGB::indexB] >> 8));
        }

        void render2PixelAverageY (PixelRGB* const dest, const uint8* src, const uint32 subPixelY) noexcept
        {
            uint32 c[3] = { 128, 128, 128 };

            const uint32 weight = 256 - subPixelY;
            c[0] += weight * src[0];
            c[1] += weight * src[1];
            c[2] += weight * src[2];

            src += this->srcData.lineStride;

            c[0] += subPixelY * src[0];
            c[1] += subPixelY * src[1];
            c[2] += subPixelY * src[2];

            dest->setARGB ((uint8) 255,
                           (uint8) (c[PixelRGB::indexR] >> 8),
                           (uint8) (c[PixelRGB::indexG] >> 8),
                           (uint8) (c[PixelRGB::indexB] >> 8));
        }

        //==============================================================================
        void render4PixelAverage (PixelAlpha* const dest, const uint8* src, const uint32 subPixelX, const uint32 subPixelY) noexcept
        {
            uint32 c = 256 * 128;
            c += src[0] * ((256 - subPixelX) * (256 - subPixelY));
            src += this->srcData.pixelStride;
            c += src[1] * (subPixelX * (256 - subPixelY));
            src += this->srcData.lineStride;
            c += src[1] * (subPixelX * subPixelY);
            src -= this->srcData.pixelStride;

            c += src[0] * ((256 - subPixelX) * subPixelY);

            *((uint8*) dest) = (uint8) (c >> 16);
        }

        void render2PixelAverageX (PixelAlpha* const dest, const uint8* src, const uint32 subPixelX) noexcept
        {
            uint32 c = 128;
            c += src[0] * (256 - subPixelX);
            src += this->srcData.pixelStride;
            c += src[0] * subPixelX;
            *((uint8*) dest) = (uint8) (c >> 8);
        }

        void render2PixelAverageY (PixelAlpha* const dest, const uint8* src, const uint32 subPixelY) noexcept
        {
            uint32 c = 128;
            c += src[0] * (256 - subPixelY);
            src += this->srcData.lineStride;
            c += src[0] * subPixelY;
            *((uint8*) dest) = (uint8) (c >> 8);
        }

        //==============================================================================
        class TransformedImageSpanInterpolator
        {
        public:
            TransformedImageSpanInterpolator (const AffineTransform& transform,
                                              const float offsetFloat, const int offsetInt) noexcept
                : inverseTransform (transform.inverted()),
                  pixelOffset (offsetFloat), pixelOffsetInt (offsetInt)
            {}

            void setStartOfLine (float sx, float sy, const int numPixels) noexcept
            {
                jassert (numPixels > 0);

                sx += pixelOffset;
                sy += pixelOffset;
                float x1 = sx, y1 = sy;
                sx += numPixels;
                inverseTransform.transformPoints (x1, y1, sx, sy);

                xBresenham.set ((int) (x1 * 256.0f), (int) (sx * 256.0f), numPixels, pixelOffsetInt);
                yBresenham.set ((int) (y1 * 256.0f), (int) (sy * 256.0f), numPixels, pixelOffsetInt);
            }

            void next (int& px, int& py) noexcept
            {
                px = xBresenham.n;  xBresenham.stepToNext();
                py = yBresenham.n;  yBresenham.stepToNext();
            }

        private:
            class BresenhamInterpolator
            {
            public:
                BresenhamInterpolator() noexcept {}

                void set (const int n1, const int n2, const int numSteps_, const int offsetInt) noexcept
                {
                    numSteps = numSteps_;
                    step = (n2 - n1) / numSteps;
                    remainder = modulo = (n2 - n1) % numSteps;
                    n = n1 + offsetInt;

                    if (modulo <= 0)
                    {
                        modulo += numSteps;
                        remainder += numSteps;
                        --step;
                    }

                    modulo -= numSteps;
                }

                forcedinline void stepToNext() noexcept
                {
                    modulo += remainder;
                    n += step;

                    if (modulo > 0)
                    {
                        modulo -= numSteps;
                        ++n;
                    }
                }

                int n;

            private:
                int numSteps, step, modulo, remainder;
            };

            const AffineTransform inverseTransform;
            BresenhamInterpolator xBresenham, yBresenham;
            const float pixelOffset;
            const int pixelOffsetInt;

            JUCE_DECLARE_NON_COPYABLE (TransformedImageSpanInterpolator)
        };

        //==============================================================================
        TransformedImageSpanInterpolator interpolator;
        const Image::BitmapData& destData;
        const Image::BitmapData& srcData;
        const int extraAlpha;
        const bool betterQuality;
        const int maxX, maxY;
        int y;
        DestPixelType* linePixels;
        HeapBlock <SrcPixelType> scratchBuffer;
        size_t scratchSize;

        JUCE_DECLARE_NON_COPYABLE (TransformedImageFill)
    };


    //==============================================================================
    template <class Iterator>
    void renderImageTransformed (Iterator& iter, const Image::BitmapData& destData, const Image::BitmapData& srcData,
                                 const int alpha, const AffineTransform& transform, bool betterQuality, bool tiledFill)
    {
        switch (destData.pixelFormat)
        {
        case Image::ARGB:
            switch (srcData.pixelFormat)
            {
            case Image::ARGB:
                if (tiledFill)  { TransformedImageFill <PixelARGB, PixelARGB, true>  r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                else            { TransformedImageFill <PixelARGB, PixelARGB, false> r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                break;
            case Image::RGB:
                if (tiledFill)  { TransformedImageFill <PixelARGB, PixelRGB, true>  r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                else            { TransformedImageFill <PixelARGB, PixelRGB, false> r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                break;
            default:
                if (tiledFill)  { TransformedImageFill <PixelARGB, PixelAlpha, true>  r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                else            { TransformedImageFill <PixelARGB, PixelAlpha, false> r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                break;
            }
            break;

        case Image::RGB:
            switch (srcData.pixelFormat)
            {
            case Image::ARGB:
                if (tiledFill)  { TransformedImageFill <PixelRGB, PixelARGB, true>  r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                else            { TransformedImageFill <PixelRGB, PixelARGB, false> r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                break;
            case Image::RGB:
                if (tiledFill)  { TransformedImageFill <PixelRGB, PixelRGB, true>  r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                else            { TransformedImageFill <PixelRGB, PixelRGB, false> r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                break;
            default:
                if (tiledFill)  { TransformedImageFill <PixelRGB, PixelAlpha, true>  r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                else            { TransformedImageFill <PixelRGB, PixelAlpha, false> r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                break;
            }
            break;

        default:
            switch (srcData.pixelFormat)
            {
            case Image::ARGB:
                if (tiledFill)  { TransformedImageFill <PixelAlpha, PixelARGB, true>  r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                else            { TransformedImageFill <PixelAlpha, PixelARGB, false> r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                break;
            case Image::RGB:
                if (tiledFill)  { TransformedImageFill <PixelAlpha, PixelRGB, true>  r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                else            { TransformedImageFill <PixelAlpha, PixelRGB, false> r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                break;
            default:
                if (tiledFill)  { TransformedImageFill <PixelAlpha, PixelAlpha, true>  r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                else            { TransformedImageFill <PixelAlpha, PixelAlpha, false> r (destData, srcData, transform, alpha, betterQuality); iter.iterate (r); }
                break;
            }
            break;
        }
    }

    template <class Iterator>
    void renderImageUntransformed (Iterator& iter, const Image::BitmapData& destData, const Image::BitmapData& srcData, const int alpha, int x, int y, bool tiledFill)
    {
        switch (destData.pixelFormat)
        {
        case Image::ARGB:
            switch (srcData.pixelFormat)
            {
            case Image::ARGB:
                if (tiledFill)  { ImageFill <PixelARGB, PixelARGB, true>  r (destData, srcData, alpha, x, y); iter.iterate (r); }
                else            { ImageFill <PixelARGB, PixelARGB, false> r (destData, srcData, alpha, x, y); iter.iterate (r); }
                break;
            case Image::RGB:
                if (tiledFill)  { ImageFill <PixelARGB, PixelRGB, true>  r (destData, srcData, alpha, x, y); iter.iterate (r); }
                else            { ImageFill <PixelARGB, PixelRGB, false> r (destData, srcData, alpha, x, y); iter.iterate (r); }
                break;
            default:
                if (tiledFill)  { ImageFill <PixelARGB, PixelAlpha, true>  r (destData, srcData, alpha, x, y); iter.iterate (r); }
                else            { ImageFill <PixelARGB, PixelAlpha, false> r (destData, srcData, alpha, x, y); iter.iterate (r); }
                break;
            }
            break;

        case Image::RGB:
            switch (srcData.pixelFormat)
            {
            case Image::ARGB:
                if (tiledFill)  { ImageFill <PixelRGB, PixelARGB, true>  r (destData, srcData, alpha, x, y); iter.iterate (r); }
                else            { ImageFill <PixelRGB, PixelARGB, false> r (destData, srcData, alpha, x, y); iter.iterate (r); }
                break;
            case Image::RGB:
                if (tiledFill)  { ImageFill <PixelRGB, PixelRGB, true>  r (destData, srcData, alpha, x, y); iter.iterate (r); }
                else            { ImageFill <PixelRGB, PixelRGB, false> r (destData, srcData, alpha, x, y); iter.iterate (r); }
                break;
            default:
                if (tiledFill)  { ImageFill <PixelRGB, PixelAlpha, true>  r (destData, srcData, alpha, x, y); iter.iterate (r); }
                else            { ImageFill <PixelRGB, PixelAlpha, false> r (destData, srcData, alpha, x, y); iter.iterate (r); }
                break;
            }
            break;

        default:
            switch (srcData.pixelFormat)
            {
            case Image::ARGB:
                if (tiledFill)  { ImageFill <PixelAlpha, PixelARGB, true>   r (destData, srcData, alpha, x, y); iter.iterate (r); }
                else            { ImageFill <PixelAlpha, PixelARGB, false>  r (destData, srcData, alpha, x, y); iter.iterate (r); }
                break;
            case Image::RGB:
                if (tiledFill)  { ImageFill <PixelAlpha, PixelRGB, true>    r (destData, srcData, alpha, x, y); iter.iterate (r); }
                else            { ImageFill <PixelAlpha, PixelRGB, false>   r (destData, srcData, alpha, x, y); iter.iterate (r); }
                break;
            default:
                if (tiledFill)  { ImageFill <PixelAlpha, PixelAlpha, true>  r (destData, srcData, alpha, x, y); iter.iterate (r); }
                else            { ImageFill <PixelAlpha, PixelAlpha, false> r (destData, srcData, alpha, x, y); iter.iterate (r); }
                break;
            }
            break;
        }
    }

    template <class Iterator, class DestPixelType>
    void renderSolidFill (Iterator& iter, const Image::BitmapData& destData, const PixelARGB& fillColour, const bool replaceContents, DestPixelType*)
    {
        if (replaceContents)
        {
            EdgeTableFillers::SolidColour <DestPixelType, true> r (destData, fillColour);
            iter.iterate (r);
        }
        else
        {
            EdgeTableFillers::SolidColour <DestPixelType, false> r (destData, fillColour);
            iter.iterate (r);
        }
    }

    template <class Iterator, class DestPixelType>
    void renderGradient (Iterator& iter, const Image::BitmapData& destData, const ColourGradient& g, const AffineTransform& transform,
                         const PixelARGB* const lookupTable, const int numLookupEntries, const bool isIdentity, DestPixelType*)
    {
        if (g.isRadial)
        {
            if (isIdentity)
            {
                EdgeTableFillers::Gradient <DestPixelType, GradientPixelIterators::Radial> renderer (destData, g, transform, lookupTable, numLookupEntries);
                iter.iterate (renderer);
            }
            else
            {
                EdgeTableFillers::Gradient <DestPixelType, GradientPixelIterators::TransformedRadial> renderer (destData, g, transform, lookupTable, numLookupEntries);
                iter.iterate (renderer);
            }
        }
        else
        {
            EdgeTableFillers::Gradient <DestPixelType, GradientPixelIterators::Linear> renderer (destData, g, transform, lookupTable, numLookupEntries);
            iter.iterate (renderer);
        }
    }
}

//==============================================================================
namespace ClipRegions
{
    class Base  : public SingleThreadedReferenceCountedObject
    {
    public:
        Base() {}
        virtual ~Base() {}

        typedef ReferenceCountedObjectPtr<Base> Ptr;

        virtual Ptr clone() const = 0;
        virtual Ptr applyClipTo (const Ptr& target) const = 0;

        virtual Ptr clipToRectangle (const Rectangle<int>&) = 0;
        virtual Ptr clipToRectangleList (const RectangleList&) = 0;
        virtual Ptr excludeClipRectangle (const Rectangle<int>&) = 0;
        virtual Ptr clipToPath (const Path&, const AffineTransform&) = 0;
        virtual Ptr clipToEdgeTable (const EdgeTable& et) = 0;
        virtual Ptr clipToImageAlpha (const Image&, const AffineTransform&, const bool betterQuality) = 0;
        virtual void translate (const Point<int>& delta) = 0;

        virtual bool clipRegionIntersects (const Rectangle<int>&) const = 0;
        virtual Rectangle<int> getClipBounds() const = 0;

        virtual void fillRectWithColour (Image::BitmapData& destData, const Rectangle<int>&, const PixelARGB& colour, bool replaceContents) const = 0;
        virtual void fillRectWithColour (Image::BitmapData& destData, const Rectangle<float>&, const PixelARGB& colour) const = 0;
        virtual void fillAllWithColour (Image::BitmapData& destData, const PixelARGB& colour, bool replaceContents) const = 0;
        virtual void fillAllWithGradient (Image::BitmapData& destData, ColourGradient&, const AffineTransform&, bool isIdentity) const = 0;
        virtual void renderImageTransformed (const Image::BitmapData& destData, const Image::BitmapData& srcData, const int alpha, const AffineTransform&, bool betterQuality, bool tiledFill) const = 0;
        virtual void renderImageUntransformed (const Image::BitmapData& destData, const Image::BitmapData& srcData, const int alpha, int x, int y, bool tiledFill) const = 0;
    };

    //==============================================================================
    class EdgeTableRegion  : public Base
    {
    public:
        EdgeTableRegion (const EdgeTable& e)        : edgeTable (e) {}
        EdgeTableRegion (const Rectangle<int>& r)   : edgeTable (r) {}
        EdgeTableRegion (const Rectangle<float>& r) : edgeTable (r) {}
        EdgeTableRegion (const RectangleList& r)    : edgeTable (r) {}
        EdgeTableRegion (const Rectangle<int>& bounds, const Path& p, const AffineTransform& t) : edgeTable (bounds, p, t) {}
        EdgeTableRegion (const EdgeTableRegion& other) : Base (other), edgeTable (other.edgeTable) {}

        Ptr clone() const                           { return new EdgeTableRegion (*this); }
        Ptr applyClipTo (const Ptr& target) const   { return target->clipToEdgeTable (edgeTable); }

        Ptr clipToRectangle (const Rectangle<int>& r)
        {
            edgeTable.clipToRectangle (r);
            return edgeTable.isEmpty() ? nullptr : this;
        }

        Ptr clipToRectangleList (const RectangleList& r)
        {
            RectangleList inverse (edgeTable.getMaximumBounds());

            if (inverse.subtract (r))
                for (const Rectangle<int>* i = inverse.begin(), * const e = inverse.end(); i != e; ++i)
                    edgeTable.excludeRectangle (*i);

            return edgeTable.isEmpty() ? nullptr : this;
        }

        Ptr excludeClipRectangle (const Rectangle<int>& r)
        {
            edgeTable.excludeRectangle (r);
            return edgeTable.isEmpty() ? nullptr : this;
        }

        Ptr clipToPath (const Path& p, const AffineTransform& transform)
        {
            EdgeTable et (edgeTable.getMaximumBounds(), p, transform);
            edgeTable.clipToEdgeTable (et);
            return edgeTable.isEmpty() ? nullptr : this;
        }

        Ptr clipToEdgeTable (const EdgeTable& et)
        {
            edgeTable.clipToEdgeTable (et);
            return edgeTable.isEmpty() ? nullptr : this;
        }

        Ptr clipToImageAlpha (const Image& image, const AffineTransform& transform, const bool betterQuality)
        {
            const Image::BitmapData srcData (image, Image::BitmapData::readOnly);

            if (transform.isOnlyTranslation())
            {
                // If our translation doesn't involve any distortion, just use a simple blit..
                const int tx = (int) (transform.getTranslationX() * 256.0f);
                const int ty = (int) (transform.getTranslationY() * 256.0f);

                if ((! betterQuality) || ((tx | ty) & 224) == 0)
                {
                    const int imageX = ((tx + 128) >> 8);
                    const int imageY = ((ty + 128) >> 8);

                    if (image.getFormat() == Image::ARGB)
                        straightClipImage (srcData, imageX, imageY, (PixelARGB*) 0);
                    else
                        straightClipImage (srcData, imageX, imageY, (PixelAlpha*) 0);

                    return edgeTable.isEmpty() ? nullptr : this;
                }
            }

            if (transform.isSingularity())
                return Ptr();

            {
                Path p;
                p.addRectangle (0, 0, (float) srcData.width, (float) srcData.height);
                EdgeTable et2 (edgeTable.getMaximumBounds(), p, transform);
                edgeTable.clipToEdgeTable (et2);
            }

            if (! edgeTable.isEmpty())
            {
                if (image.getFormat() == Image::ARGB)
                    transformedClipImage (srcData, transform, betterQuality, (PixelARGB*) 0);
                else
                    transformedClipImage (srcData, transform, betterQuality, (PixelAlpha*) 0);
            }

            return edgeTable.isEmpty() ? nullptr : this;
        }

        void translate (const Point<int>& delta)
        {
            edgeTable.translate ((float) delta.x, delta.y);
        }

        bool clipRegionIntersects (const Rectangle<int>& r) const
        {
            return edgeTable.getMaximumBounds().intersects (r);
        }

        Rectangle<int> getClipBounds() const
        {
            return edgeTable.getMaximumBounds();
        }

        void fillRectWithColour (Image::BitmapData& destData, const Rectangle<int>& area, const PixelARGB& colour, bool replaceContents) const
        {
            const Rectangle<int> totalClip (edgeTable.getMaximumBounds());
            const Rectangle<int> clipped (totalClip.getIntersection (area));

            if (! clipped.isEmpty())
            {
                EdgeTableRegion et (clipped);
                et.edgeTable.clipToEdgeTable (edgeTable);
                et.fillAllWithColour (destData, colour, replaceContents);
            }
        }

        void fillRectWithColour (Image::BitmapData& destData, const Rectangle<float>& area, const PixelARGB& colour) const
        {
            const Rectangle<float> totalClip (edgeTable.getMaximumBounds().toFloat());
            const Rectangle<float> clipped (totalClip.getIntersection (area));

            if (! clipped.isEmpty())
            {
                EdgeTableRegion et (clipped);
                et.edgeTable.clipToEdgeTable (edgeTable);
                et.fillAllWithColour (destData, colour, false);
            }
        }

        void fillAllWithColour (Image::BitmapData& destData, const PixelARGB& colour, bool replaceContents) const
        {
            switch (destData.pixelFormat)
            {
                case Image::ARGB:   EdgeTableFillers::renderSolidFill (edgeTable, destData, colour, replaceContents, (PixelARGB*) 0); break;
                case Image::RGB:    EdgeTableFillers::renderSolidFill (edgeTable, destData, colour, replaceContents, (PixelRGB*) 0); break;
                default:            EdgeTableFillers::renderSolidFill (edgeTable, destData, colour, replaceContents, (PixelAlpha*) 0); break;
            }
        }

        void fillAllWithGradient (Image::BitmapData& destData, ColourGradient& gradient, const AffineTransform& transform, bool isIdentity) const
        {
            HeapBlock <PixelARGB> lookupTable;
            const int numLookupEntries = gradient.createLookupTable (transform, lookupTable);
            jassert (numLookupEntries > 0);

            switch (destData.pixelFormat)
            {
                case Image::ARGB:   EdgeTableFillers::renderGradient (edgeTable, destData, gradient, transform, lookupTable, numLookupEntries, isIdentity, (PixelARGB*) 0); break;
                case Image::RGB:    EdgeTableFillers::renderGradient (edgeTable, destData, gradient, transform, lookupTable, numLookupEntries, isIdentity, (PixelRGB*) 0); break;
                default:            EdgeTableFillers::renderGradient (edgeTable, destData, gradient, transform, lookupTable, numLookupEntries, isIdentity, (PixelAlpha*) 0); break;
            }
        }

        void renderImageTransformed (const Image::BitmapData& destData, const Image::BitmapData& srcData, const int alpha, const AffineTransform& transform, bool betterQuality, bool tiledFill) const
        {
            EdgeTableFillers::renderImageTransformed (edgeTable, destData, srcData, alpha, transform, betterQuality, tiledFill);
        }

        void renderImageUntransformed (const Image::BitmapData& destData, const Image::BitmapData& srcData, const int alpha, int x, int y, bool tiledFill) const
        {
            EdgeTableFillers::renderImageUntransformed (edgeTable, destData, srcData, alpha, x, y, tiledFill);
        }

        EdgeTable edgeTable;

    private:
        //==============================================================================
        template <class SrcPixelType>
        void transformedClipImage (const Image::BitmapData& srcData, const AffineTransform& transform, const bool betterQuality, const SrcPixelType*)
        {
            EdgeTableFillers::TransformedImageFill <SrcPixelType, SrcPixelType, false> renderer (srcData, srcData, transform, 255, betterQuality);

            for (int y = 0; y < edgeTable.getMaximumBounds().getHeight(); ++y)
                renderer.clipEdgeTableLine (edgeTable, edgeTable.getMaximumBounds().getX(), y + edgeTable.getMaximumBounds().getY(),
                                            edgeTable.getMaximumBounds().getWidth());
        }

        template <class SrcPixelType>
        void straightClipImage (const Image::BitmapData& srcData, int imageX, int imageY, const SrcPixelType*)
        {
            Rectangle<int> r (imageX, imageY, srcData.width, srcData.height);
            edgeTable.clipToRectangle (r);

            EdgeTableFillers::ImageFill <SrcPixelType, SrcPixelType, false> renderer (srcData, srcData, 255, imageX, imageY);

            for (int y = 0; y < r.getHeight(); ++y)
                renderer.clipEdgeTableLine (edgeTable, r.getX(), y + r.getY(), r.getWidth());
        }

        EdgeTableRegion& operator= (const EdgeTableRegion&);
    };

    //==============================================================================
    class RectangleListRegion  : public Base
    {
    public:
        RectangleListRegion (const Rectangle<int>& r) : clip (r) {}
        RectangleListRegion (const RectangleList& r)  : clip (r) {}
        RectangleListRegion (const RectangleListRegion& other) : Base (other), clip (other.clip) {}

        Ptr clone() const                           { return new RectangleListRegion (*this); }
        Ptr applyClipTo (const Ptr& target) const   { return target->clipToRectangleList (clip); }

        Ptr clipToRectangle (const Rectangle<int>& r)
        {
            clip.clipTo (r);
            return clip.isEmpty() ? nullptr : this;
        }

        Ptr clipToRectangleList (const RectangleList& r)
        {
            clip.clipTo (r);
            return clip.isEmpty() ? nullptr : this;
        }

        Ptr excludeClipRectangle (const Rectangle<int>& r)
        {
            clip.subtract (r);
            return clip.isEmpty() ? nullptr : this;
        }

        Ptr clipToPath (const Path& p, const AffineTransform& transform)  { return toEdgeTable()->clipToPath (p, transform); }
        Ptr clipToEdgeTable (const EdgeTable& et)                         { return toEdgeTable()->clipToEdgeTable (et); }

        Ptr clipToImageAlpha (const Image& image, const AffineTransform& transform, const bool betterQuality)
        {
            return toEdgeTable()->clipToImageAlpha (image, transform, betterQuality);
        }

        void translate (const Point<int>& delta)
        {
            clip.offsetAll (delta.x, delta.y);
        }

        bool clipRegionIntersects (const Rectangle<int>& r) const   { return clip.intersects (r); }
        Rectangle<int> getClipBounds() const                        { return clip.getBounds(); }

        void fillRectWithColour (Image::BitmapData& destData, const Rectangle<int>& area, const PixelARGB& colour, bool replaceContents) const
        {
            SubRectangleIterator iter (clip, area);

            switch (destData.pixelFormat)
            {
                case Image::ARGB:   EdgeTableFillers::renderSolidFill (iter, destData, colour, replaceContents, (PixelARGB*) 0); break;
                case Image::RGB:    EdgeTableFillers::renderSolidFill (iter, destData, colour, replaceContents, (PixelRGB*) 0); break;
                default:            EdgeTableFillers::renderSolidFill (iter, destData, colour, replaceContents, (PixelAlpha*) 0); break;
            }
        }

        void fillRectWithColour (Image::BitmapData& destData, const Rectangle<float>& area, const PixelARGB& colour) const
        {
            SubRectangleIteratorFloat iter (clip, area);

            switch (destData.pixelFormat)
            {
                case Image::ARGB:   EdgeTableFillers::renderSolidFill (iter, destData, colour, false, (PixelARGB*) 0); break;
                case Image::RGB:    EdgeTableFillers::renderSolidFill (iter, destData, colour, false, (PixelRGB*) 0); break;
                default:            EdgeTableFillers::renderSolidFill (iter, destData, colour, false, (PixelAlpha*) 0); break;
            }
        }

        void fillAllWithColour (Image::BitmapData& destData, const PixelARGB& colour, bool replaceContents) const
        {
            switch (destData.pixelFormat)
            {
                case Image::ARGB:   EdgeTableFillers::renderSolidFill (*this, destData, colour, replaceContents, (PixelARGB*) 0); break;
                case Image::RGB:    EdgeTableFillers::renderSolidFill (*this, destData, colour, replaceContents, (PixelRGB*) 0); break;
                default:            EdgeTableFillers::renderSolidFill (*this, destData, colour, replaceContents, (PixelAlpha*) 0); break;
            }
        }

        void fillAllWithGradient (Image::BitmapData& destData, ColourGradient& gradient, const AffineTransform& transform, bool isIdentity) const
        {
            HeapBlock <PixelARGB> lookupTable;
            const int numLookupEntries = gradient.createLookupTable (transform, lookupTable);
            jassert (numLookupEntries > 0);

            switch (destData.pixelFormat)
            {
                case Image::ARGB:   EdgeTableFillers::renderGradient (*this, destData, gradient, transform, lookupTable, numLookupEntries, isIdentity, (PixelARGB*) 0); break;
                case Image::RGB:    EdgeTableFillers::renderGradient (*this, destData, gradient, transform, lookupTable, numLookupEntries, isIdentity, (PixelRGB*) 0); break;
                default:            EdgeTableFillers::renderGradient (*this, destData, gradient, transform, lookupTable, numLookupEntries, isIdentity, (PixelAlpha*) 0); break;
            }
        }

        void renderImageTransformed (const Image::BitmapData& destData, const Image::BitmapData& srcData, const int alpha, const AffineTransform& transform, bool betterQuality, bool tiledFill) const
        {
            EdgeTableFillers::renderImageTransformed (*this, destData, srcData, alpha, transform, betterQuality, tiledFill);
        }

        void renderImageUntransformed (const Image::BitmapData& destData, const Image::BitmapData& srcData, const int alpha, int x, int y, bool tiledFill) const
        {
            EdgeTableFillers::renderImageUntransformed (*this, destData, srcData, alpha, x, y, tiledFill);
        }

        RectangleList clip;

        //==============================================================================
        template <class Renderer>
        void iterate (Renderer& r) const noexcept
        {
            for (const Rectangle<int>* i = clip.begin(), * const e = clip.end(); i != e; ++i)
            {
                const int x = i->getX();
                const int w = i->getWidth();
                jassert (w > 0);
                const int bottom = i->getBottom();

                for (int y = i->getY(); y < bottom; ++y)
                {
                    r.setEdgeTableYPos (y);
                    r.handleEdgeTableLineFull (x, w);
                }
            }
        }

    private:
        //==============================================================================
        class SubRectangleIterator
        {
        public:
            SubRectangleIterator (const RectangleList& clipList, const Rectangle<int>& clipBounds)
                : clip (clipList), area (clipBounds)
            {}

            template <class Renderer>
            void iterate (Renderer& r) const noexcept
            {
                for (const Rectangle<int>* i = clip.begin(), * const e = clip.end(); i != e; ++i)
                {
                    const Rectangle<int> rect (i->getIntersection (area));

                    if (! rect.isEmpty())
                    {
                        const int x = rect.getX();
                        const int w = rect.getWidth();
                        const int bottom = rect.getBottom();

                        for (int y = rect.getY(); y < bottom; ++y)
                        {
                            r.setEdgeTableYPos (y);
                            r.handleEdgeTableLineFull (x, w);
                        }
                    }
                }
            }

        private:
            const RectangleList& clip;
            const Rectangle<int> area;

            JUCE_DECLARE_NON_COPYABLE (SubRectangleIterator)
        };

        //==============================================================================
        class SubRectangleIteratorFloat
        {
        public:
            SubRectangleIteratorFloat (const RectangleList& clipList, const Rectangle<float>& clipBounds) noexcept
                : clip (clipList), area (clipBounds)
            {
            }

            template <class Renderer>
            void iterate (Renderer& r) const noexcept
            {
                const RenderingHelpers::FloatRectangleRasterisingInfo f (area);

                for (const Rectangle<int>* i = clip.begin(), * const e = clip.end(); i != e; ++i)
                {
                    const int clipLeft   = i->getX();
                    const int clipRight  = i->getRight();
                    const int clipTop    = i->getY();
                    const int clipBottom = i->getBottom();

                    if (f.totalBottom > clipTop && f.totalTop < clipBottom && f.totalRight > clipLeft && f.totalLeft < clipRight)
                    {
                        if (f.isOnePixelWide())
                        {
                            if (f.topAlpha != 0 && f.totalTop >= clipTop)
                            {
                                r.setEdgeTableYPos (f.totalTop);
                                r.handleEdgeTablePixel (f.left, f.topAlpha);
                            }

                            const int endY = jmin (f.bottom, clipBottom);
                            for (int y = jmax (clipTop, f.top); y < endY; ++y)
                            {
                                r.setEdgeTableYPos (y);
                                r.handleEdgeTablePixelFull (f.left);
                            }

                            if (f.bottomAlpha != 0 && f.bottom < clipBottom)
                            {
                                r.setEdgeTableYPos (f.bottom);
                                r.handleEdgeTablePixel (f.left, f.bottomAlpha);
                            }
                        }
                        else
                        {
                            const int clippedLeft   = jmax (f.left, clipLeft);
                            const int clippedWidth  = jmin (f.right, clipRight) - clippedLeft;
                            const bool doLeftAlpha  = f.leftAlpha != 0 && f.totalLeft >= clipLeft;
                            const bool doRightAlpha = f.rightAlpha != 0 && f.right < clipRight;

                            if (f.topAlpha != 0 && f.totalTop >= clipTop)
                            {
                                r.setEdgeTableYPos (f.totalTop);

                                if (doLeftAlpha)        r.handleEdgeTablePixel (f.totalLeft, f.getTopLeftCornerAlpha());
                                if (clippedWidth > 0)   r.handleEdgeTableLine (clippedLeft, clippedWidth, f.topAlpha);
                                if (doRightAlpha)       r.handleEdgeTablePixel (f.right, f.getTopRightCornerAlpha());
                            }

                            const int endY = jmin (f.bottom, clipBottom);
                            for (int y = jmax (clipTop, f.top); y < endY; ++y)
                            {
                                r.setEdgeTableYPos (y);

                                if (doLeftAlpha)        r.handleEdgeTablePixel (f.totalLeft, f.leftAlpha);
                                if (clippedWidth > 0)   r.handleEdgeTableLineFull (clippedLeft, clippedWidth);
                                if (doRightAlpha)       r.handleEdgeTablePixel (f.right, f.rightAlpha);
                            }

                            if (f.bottomAlpha != 0 && f.bottom < clipBottom)
                            {
                                r.setEdgeTableYPos (f.bottom);

                                if (doLeftAlpha)        r.handleEdgeTablePixel (f.totalLeft, f.getBottomLeftCornerAlpha());
                                if (clippedWidth > 0)   r.handleEdgeTableLine (clippedLeft, clippedWidth, f.bottomAlpha);
                                if (doRightAlpha)       r.handleEdgeTablePixel (f.right, f.getBottomRightCornerAlpha());
                            }
                        }
                    }
                }
            }

        private:
            const RectangleList& clip;
            const Rectangle<float>& area;

            JUCE_DECLARE_NON_COPYABLE (SubRectangleIteratorFloat)
        };

        inline Ptr toEdgeTable() const   { return new EdgeTableRegion (clip); }

        RectangleListRegion& operator= (const RectangleListRegion&);
    };
}

//==============================================================================
class SoftwareRendererSavedState
{
public:
    SoftwareRendererSavedState (const Image& im, const Rectangle<int>& clipBounds)
        : image (im), clip (new ClipRegions::RectangleListRegion (clipBounds)),
          transform (0, 0),
          interpolationQuality (Graphics::mediumResamplingQuality),
          transparencyLayerAlpha (1.0f)
    {
    }

    SoftwareRendererSavedState (const Image& im, const RectangleList& clipList, const int x, const int y)
        : image (im), clip (new ClipRegions::RectangleListRegion (clipList)),
          transform (x, y),
          interpolationQuality (Graphics::mediumResamplingQuality),
          transparencyLayerAlpha (1.0f)
    {
    }

    SoftwareRendererSavedState (const SoftwareRendererSavedState& other)
        : image (other.image), clip (other.clip), transform (other.transform),
          font (other.font), fillType (other.fillType),
          interpolationQuality (other.interpolationQuality),
          transparencyLayerAlpha (other.transparencyLayerAlpha)
    {
    }

    bool clipToRectangle (const Rectangle<int>& r)
    {
        if (clip != nullptr)
        {
            if (transform.isOnlyTranslated)
            {
                cloneClipIfMultiplyReferenced();
                clip = clip->clipToRectangle (transform.translated (r));
            }
            else if (transform.isIntegerScaling)
            {
                cloneClipIfMultiplyReferenced();
                clip = clip->clipToRectangle (transform.transformed (r).getSmallestIntegerContainer());
            }
            else
            {
                Path p;
                p.addRectangle (r);
                clipToPath (p, AffineTransform::identity);
            }
        }

        return clip != nullptr;
    }

    bool clipToRectangleList (const RectangleList& r)
    {
        if (clip != nullptr)
        {
            if (transform.isOnlyTranslated)
            {
                cloneClipIfMultiplyReferenced();
                RectangleList offsetList (r);
                offsetList.offsetAll (transform.xOffset, transform.yOffset);
                clip = clip->clipToRectangleList (offsetList);
            }
            else if (transform.isIntegerScaling)
            {
                cloneClipIfMultiplyReferenced();
                RectangleList scaledList;

                for (const Rectangle<int>* i = r.begin(), * const e = r.end(); i != e; ++i)
                    scaledList.add (transform.transformed (*i).getSmallestIntegerContainer());

                clip = clip->clipToRectangleList (scaledList);
            }
            else
            {
                clipToPath (r.toPath(), AffineTransform::identity);
            }
        }

        return clip != nullptr;
    }

    bool excludeClipRectangle (const Rectangle<int>& r)
    {
        if (clip != nullptr)
        {
            cloneClipIfMultiplyReferenced();

            if (transform.isOnlyTranslated)
            {
                clip = clip->excludeClipRectangle (transform.translated (r));
            }
            else if (transform.isIntegerScaling)
            {
                clip = clip->excludeClipRectangle (transform.transformed (r).getSmallestIntegerContainer());
            }
            else
            {
                Path p;
                p.addRectangle (r.toFloat());
                p.applyTransform (transform.complexTransform);
                p.addRectangle (clip->getClipBounds().toFloat());
                p.setUsingNonZeroWinding (false);
                clip = clip->clipToPath (p, AffineTransform::identity);
            }
        }

        return clip != nullptr;
    }

    void clipToPath (const Path& p, const AffineTransform& t)
    {
        if (clip != nullptr)
        {
            cloneClipIfMultiplyReferenced();
            clip = clip->clipToPath (p, transform.getTransformWith (t));
        }
    }

    void clipToImageAlpha (const Image& sourceImage, const AffineTransform& t)
    {
        if (clip != nullptr)
        {
            if (sourceImage.hasAlphaChannel())
            {
                cloneClipIfMultiplyReferenced();
                clip = clip->clipToImageAlpha (sourceImage, transform.getTransformWith (t),
                                               interpolationQuality != Graphics::lowResamplingQuality);
            }
            else
            {
                Path p;
                p.addRectangle (sourceImage.getBounds());
                clipToPath (p, t);
            }
        }
    }

    bool clipRegionIntersects (const Rectangle<int>& r) const
    {
        if (clip != nullptr)
        {
            if (transform.isOnlyTranslated)
                return clip->clipRegionIntersects (transform.translated (r));

            return getClipBounds().intersects (r);
        }

        return false;
    }

    Rectangle<int> getClipBounds() const
    {
        return clip != nullptr ? transform.deviceSpaceToUserSpace (clip->getClipBounds())
                               : Rectangle<int>();
    }

    SoftwareRendererSavedState* beginTransparencyLayer (float opacity)
    {
        SoftwareRendererSavedState* s = new SoftwareRendererSavedState (*this);

        if (clip != nullptr)
        {
            const Rectangle<int> layerBounds (clip->getClipBounds());

            s->image = Image (Image::ARGB, layerBounds.getWidth(), layerBounds.getHeight(), true);
            s->transparencyLayerAlpha = opacity;
            s->transform.moveOriginInDeviceSpace (-layerBounds.getX(), -layerBounds.getY());

            s->cloneClipIfMultiplyReferenced();
            s->clip->translate (-layerBounds.getPosition());
        }

        return s;
    }

    void endTransparencyLayer (SoftwareRendererSavedState& finishedLayerState)
    {
        if (clip != nullptr)
        {
            const Rectangle<int> layerBounds (clip->getClipBounds());

            const ScopedPointer<LowLevelGraphicsContext> g (image.createLowLevelContext());
            g->setOpacity (finishedLayerState.transparencyLayerAlpha);
            g->drawImage (finishedLayerState.image, AffineTransform::translation ((float) layerBounds.getX(),
                                                                                  (float) layerBounds.getY()));
        }
    }

    //==============================================================================
    void fillTargetRect (const Rectangle<int>& r, const bool replaceContents)
    {
        if (fillType.isColour())
        {
            Image::BitmapData destData (image, Image::BitmapData::readWrite);
            clip->fillRectWithColour (destData, r, fillType.colour.getPixelARGB(), replaceContents);
        }
        else
        {
            const Rectangle<int> clipped (clip->getClipBounds().getIntersection (r));

            if (! clipped.isEmpty())
                fillShape (new ClipRegions::RectangleListRegion (clipped), false);
        }
    }

    void fillTargetRect (const Rectangle<float>& r)
    {
        if (fillType.isColour())
        {
            Image::BitmapData destData (image, Image::BitmapData::readWrite);
            clip->fillRectWithColour (destData, r, fillType.colour.getPixelARGB());
        }
        else
        {
            const Rectangle<float> clipped (clip->getClipBounds().toFloat().getIntersection (r));

            if (! clipped.isEmpty())
                fillShape (new ClipRegions::EdgeTableRegion (clipped), false);
        }
    }

    template <typename CoordType>
    void fillRectAsPath (const Rectangle<CoordType>& r)
    {
        Path p;
        p.addRectangle (r);
        fillPath (p, AffineTransform::identity);
    }

    void fillRect (const Rectangle<int>& r, const bool replaceContents)
    {
        if (clip != nullptr)
        {
            if (transform.isOnlyTranslated)
                fillTargetRect (transform.translated (r), replaceContents);
            else if (transform.isIntegerScaling)
                fillTargetRect (transform.transformed (r).getSmallestIntegerContainer(), replaceContents);
            else
                fillRectAsPath (r);
        }
    }

    void fillRect (const Rectangle<float>& r)
    {
        if (clip != nullptr)
        {
            if (transform.isOnlyTranslated)
                fillTargetRect (transform.translated (r));
            else if (transform.isIntegerScaling)
                fillTargetRect (transform.transformed (r));
            else
                fillRectAsPath (r);
        }
    }

    void fillPath (const Path& path, const AffineTransform& t)
    {
        if (clip != nullptr)
            fillShape (new ClipRegions::EdgeTableRegion (clip->getClipBounds(), path, transform.getTransformWith (t)), false);
    }

    void fillEdgeTable (const EdgeTable& edgeTable, const float x, const int y)
    {
        jassert (transform.isOnlyTranslated);

        if (clip != nullptr)
        {
            ClipRegions::EdgeTableRegion* edgeTableClip = new ClipRegions::EdgeTableRegion (edgeTable);
            edgeTableClip->edgeTable.translate (x + transform.xOffset,
                                                y + transform.yOffset);
            fillShape (edgeTableClip, false);
        }
    }

    void drawGlyph (const Font& f, int glyphNumber, const AffineTransform& t)
    {
        if (clip != nullptr)
        {
            const ScopedPointer<EdgeTable> et (f.getTypeface()->getEdgeTableForGlyph (glyphNumber, transform.getTransformWith (t)));

            if (et != nullptr)
                fillShape (new ClipRegions::EdgeTableRegion (*et), false);
        }
    }

    void fillShape (ClipRegions::Base::Ptr shapeToFill, const bool replaceContents)
    {
        jassert (clip != nullptr);

        shapeToFill = clip->applyClipTo (shapeToFill);

        if (shapeToFill != nullptr)
        {
            Image::BitmapData destData (image, Image::BitmapData::readWrite);

            if (fillType.isGradient())
            {
                jassert (! replaceContents); // that option is just for solid colours

                ColourGradient g2 (*(fillType.gradient));
                g2.multiplyOpacity (fillType.getOpacity());
                AffineTransform t (transform.getTransformWith (fillType.transform).translated (-0.5f, -0.5f));

                const bool isIdentity = t.isOnlyTranslation();

                if (isIdentity)
                {
                    // If our translation doesn't involve any distortion, we can speed it up..
                    g2.point1.applyTransform (t);
                    g2.point2.applyTransform (t);
                    t = AffineTransform::identity;
                }

                shapeToFill->fillAllWithGradient (destData, g2, t, isIdentity);
            }
            else if (fillType.isTiledImage())
            {
                renderImage (fillType.image, fillType.transform, shapeToFill);
            }
            else
            {
                shapeToFill->fillAllWithColour (destData, fillType.colour.getPixelARGB(), replaceContents);
            }
        }
    }

    //==============================================================================
    void renderImage (const Image& sourceImage, const AffineTransform& trans,
                      const ClipRegions::Base* const tiledFillClipRegion)
    {
        const AffineTransform t (transform.getTransformWith (trans));

        const Image::BitmapData destData (image, Image::BitmapData::readWrite);
        const Image::BitmapData srcData (sourceImage, Image::BitmapData::readOnly);
        const int alpha = fillType.colour.getAlpha();
        const bool betterQuality = (interpolationQuality != Graphics::lowResamplingQuality);

        if (t.isOnlyTranslation())
        {
            // If our translation doesn't involve any distortion, just use a simple blit..
            int tx = (int) (t.getTranslationX() * 256.0f);
            int ty = (int) (t.getTranslationY() * 256.0f);

            if ((! betterQuality) || ((tx | ty) & 224) == 0)
            {
                tx = ((tx + 128) >> 8);
                ty = ((ty + 128) >> 8);

                if (tiledFillClipRegion != nullptr)
                {
                    tiledFillClipRegion->renderImageUntransformed (destData, srcData, alpha, tx, ty, true);
                }
                else
                {
                    Rectangle<int> area (tx, ty, sourceImage.getWidth(), sourceImage.getHeight());
                    area = area.getIntersection (image.getBounds());

                    if (! area.isEmpty())
                    {
                        ClipRegions::Base::Ptr c (clip->applyClipTo (new ClipRegions::EdgeTableRegion (area)));

                        if (c != nullptr)
                            c->renderImageUntransformed (destData, srcData, alpha, tx, ty, false);
                    }
                }

                return;
            }
        }

        if (! t.isSingularity())
        {
            if (tiledFillClipRegion != nullptr)
            {
                tiledFillClipRegion->renderImageTransformed (destData, srcData, alpha, t, betterQuality, true);
            }
            else
            {
                Path p;
                p.addRectangle (sourceImage.getBounds());

                ClipRegions::Base::Ptr c (clip->clone());
                c = c->clipToPath (p, t);

                if (c != nullptr)
                    c->renderImageTransformed (destData, srcData, alpha, t, betterQuality, false);
            }
        }
    }

    //==============================================================================
    Image image;
    ClipRegions::Base::Ptr clip;
    RenderingHelpers::TranslationOrTransform transform;
    Font font;
    FillType fillType;
    Graphics::ResamplingQuality interpolationQuality;

private:
    float transparencyLayerAlpha;

    void cloneClipIfMultiplyReferenced()
    {
        if (clip->getReferenceCount() > 1)
            clip = clip->clone();
    }

    SoftwareRendererSavedState& operator= (const SoftwareRendererSavedState&);
};

//==============================================================================
template <class StateObjectType>
class SavedStateStack
{
public:
    SavedStateStack (StateObjectType* const initialState) noexcept
        : currentState (initialState)
    {}

    inline StateObjectType* operator->() const noexcept     { return currentState; }
    inline StateObjectType& operator*()  const noexcept     { return *currentState; }

    void save()
    {
        stack.add (new StateObjectType (*currentState));
    }

    void restore()
    {
        if (StateObjectType* const top = stack.getLast())
        {
            currentState = top;
            stack.removeLast (1, false);
        }
        else
        {
            jassertfalse; // trying to pop with an empty stack!
        }
    }

    void beginTransparencyLayer (float opacity)
    {
        save();
        currentState = currentState->beginTransparencyLayer (opacity);
    }

    void endTransparencyLayer()
    {
        const ScopedPointer<StateObjectType> finishedTransparencyLayer (currentState);
        restore();
        currentState->endTransparencyLayer (*finishedTransparencyLayer);
    }

private:
    ScopedPointer<StateObjectType> currentState;
    OwnedArray<StateObjectType> stack;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SavedStateStack)
};

}

#if JUCE_MSVC
 #pragma warning (pop)
#endif

#endif   // __JUCE_RENDERINGHELPERS_JUCEHEADER__
