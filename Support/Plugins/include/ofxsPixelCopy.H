#ifndef _ofxsPixelCopy_h_
#define _ofxsPixelCopy_h_
// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

/** @file This file contains the straight pixel pass-through and metadata value
formatting shared by the metadata example plugins.
*/

#include <cctype>
#include <cstring>
#include <sstream>
#include <string>

#include "ofxsImageEffect.h"
#include "ofxsMetadata.h"

namespace OFX {

    /** @brief bytes per pixel of image, given its depth and component count, or 0 if the depth is unrecognised */
    inline int bytesPerPixel(const OFX::Image &image)
    {
        int perComponent = 0;

        switch(image.getPixelDepth()) {
        case OFX::eBitDepthUByte  : perComponent = 1; break;
        case OFX::eBitDepthUShort : perComponent = 2; break;
        case OFX::eBitDepthHalf   : perComponent = 2; break;
        case OFX::eBitDepthFloat  : perComponent = 4; break;
        default : return 0;
        }

        return perComponent * image.getPixelComponentCount();
    }

    /** @brief copies src to dst over window, zeroing dst pixels that have no corresponding src pixel */
    inline void copyPixels(const OFX::Image &src, OFX::Image &dst, const OfxRectI &window)
    {
        const int pixelBytes = bytesPerPixel(dst);

        if(pixelBytes == 0 || pixelBytes != bytesPerPixel(src))
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

        for(int y = window.y1; y < window.y2; y++) {
            for(int x = window.x1; x < window.x2; x++) {
                void *to = dst.getPixelAddress(x, y);

                if(!to)
                    continue;

                if(const void *from = src.getPixelAddress(x, y))
                    memcpy(to, from, size_t(pixelBytes));
                else
                    memset(to, 0, size_t(pixelBytes));
            }
        }
    }

    /** @brief every value of a key, comma separated, read back as the type the host holds it as */
    inline std::string valueText(const OFX::MetadataSet &metadata, const OFX::MetadataEntry &entry)
    {
        std::ostringstream text;

        for(int i = 0; i < entry.dimension; i++) {
            if(i)
                text << ",";

            switch(entry.type) {
            case OFX::eMetadataTypeInt    : text << metadata.getInt(entry.key, i); break;
            case OFX::eMetadataTypeDouble : text << metadata.getDouble(entry.key, i); break;
            default                       : text << metadata.getString(entry.key, i); break;
            }
        }

        return text.str();
    }

    /** @brief c, folded to lower case */
    inline char lowerCase(char c)
    {
        return char(tolower((unsigned char) c));
    }

}

#endif
