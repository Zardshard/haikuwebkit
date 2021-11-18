/*
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "SourceGraphic.h"

#include "Filter.h"
#include "GraphicsContext.h"
#include <wtf/text/TextStream.h>

namespace WebCore {

Ref<SourceGraphic> SourceGraphic::create()
{
    return adoptRef(*new SourceGraphic());
}

SourceGraphic::SourceGraphic()
    : FilterEffect(FilterEffect::Type::SourceGraphic)
{
    setOperatingColorSpace(DestinationColorSpace::SRGB());
}

void SourceGraphic::determineAbsolutePaintRect(const Filter& filter)
{
    FloatRect paintRect = filter.sourceImageRect();
    setAbsolutePaintRect(enclosingIntRect(paintRect));
}

void SourceGraphic::platformApplySoftware(const Filter& filter)
{
    ImageBuffer* resultImage = createImageBufferResult();
    ImageBuffer* sourceImage = filter.sourceImage();
    if (!resultImage || !sourceImage)
        return;

    resultImage->context().drawImageBuffer(*sourceImage, IntPoint());
}

TextStream& SourceGraphic::externalRepresentation(TextStream& ts, RepresentationType) const
{
    ts << indent << "[SourceGraphic]\n";
    return ts;
}

} // namespace WebCore
