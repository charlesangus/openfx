#ifndef _ofxMetadata_h_
#define _ofxMetadata_h_

#include "ofxCore.h"
#include "ofxImageEffect.h"

// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause


#ifdef __cplusplus
extern "C" {
#endif

/** @file ofxMetadata.h
API for retrieving host- and format-defined metadata attached to a clip's images,
for example metadata originating from a file's container or from upstream processing.

Metadata is a flat property set. Each key is a string, and its value is an int,
a double or a string, or an array of one of those; there is no nesting and no
binary blob type.

The key space is divided into namespaces by a prefix ending in a forward slash:

- ``ofx/`` is reserved for the standard, host-independent keys defined in this
  file. Neither a host nor a plugin may invent new keys in this namespace.
- ``exr/``, ``exif/``, ``dpx/``, ``cin/``, ``tiff/`` and ``quicktime/`` are
  reserved for keys carried verbatim from the file format an image was read
  from. A host that cannot produce format-prefixed keys may omit them entirely;
  these prefixes are reserved, not mandatory.
- any other key must be named in reverse DNS form, using a domain the definer
  controls, for example ``com.example.mytool.regionOfInterest``. This applies
  equally to host- and plugin-defined keys.

A host publishes a standard key only when it knows the value. A value that is
not known must be omitted rather than published as an empty string, a zero or
any other placeholder, so that a plugin can distinguish "not available" from a
genuine value. Except where a key's documentation states otherwise, a host that
does know the value of a standard key should publish it under that key rather
than under a vendor-specific name.

@version Added in OpenFX NEXT
*/


/** @brief the string that names the MetadataSuite, passed to OfxHost::fetchSuite */
#define kOfxMetadataSuite "OfxMetadataSuite"

/** @brief Action called to retrieve the metadata an effect contributes for a clip at a given time.

Metadata is a property of an image — a clip at a specific time — and this action is always time-parameterised.
The host calls this action whenever the effect's parameter or input state changes, using the same hash it
already uses for the render cache, so no separate invalidation property is required or defined.

An effect may add or modify metadata properties in the metadata property set returned via the suite,
or it may choose not to modify metadata at all.

 @param handle handle to the instance, cast to an \ref OfxImageEffectHandle

 @param inArgs has the following properties
     - \ref kOfxPropTime the time at which the metadata is being requested

 @param outArgs is a property set that the effect populates with the metadata it contributes.
 It also carries the following properties, which describe how metadata is inherited from the
 effect's input clips
     - \ref kOfxImageEffectPropMetadataSourceClip the name of the input clip whose metadata the
       output inherits, defaulting to the first input clip described by the effect
     - a set of char * X N properties, one for each of the input clips currently attached,
       labelled with ``OfxImageClipPropMetadataRetainedKeys_`` post pended with the clip's name,
       for example ``OfxImageClipPropMetadataRetainedKeys_Source``. Each such property lists the
       metadata keys retained from that input clip. A key absent from the list on a clip is not
       carried through from that clip. The host initialises each of these to the full set of keys
       present on the clip named by \ref kOfxImageEffectPropMetadataSourceClip and to the empty
       list on every other input clip, before the action is called.

 @returns
     - \ref kOfxStatOK the action was trapped and the effect has populated outArgs with the metadata it contributes,
     - \ref kOfxStatReplyDefault the action was not trapped and the host should use its default metadata,
     - \ref kOfxStatErrMemory the host ran out of memory, in which case the action may be called again after a memory purge,
     - \ref kOfxStatFailed something went wrong but no error code is appropriate, the plugin should post a message,
     - \ref kOfxStatErrFatal

 @version Added in OpenFX NEXT

    @actiondef
    inArgs:
      - OfxPropTime
    outArgs:
      - OfxImageEffectPropMetadataSourceClip
    # this special prop has the clip name postpended after "_"
    # - OfxImageClipPropMetadataRetainedKeys_
 */
#define kOfxImageEffectActionGetMetadata "OfxImageEffectActionGetMetadata"

/** @brief The name of the input clip whose metadata the output clip inherits

An effect sets this in the ``outArgs`` of \ref kOfxImageEffectActionGetMetadata to nominate
the single input clip that the output clip inherits metadata from. Every metadata key present
on that input's images at the time being rendered is carried through to the output, subject to
the per-clip ``OfxImageClipPropMetadataRetainedKeys_`` properties described in that action.

   - Type - string X 1
   - Property Set - outArgs property set of the \ref kOfxImageEffectActionGetMetadata action
   - Valid Values - the name of any of the effect's input clips, or the empty string for an
                    output that inherits no metadata
   - Default - the name of the first input clip described by the effect, or the empty string
               if the effect has no input clips

 @version Added in OpenFX NEXT

   @propdef
   type: string
   dimension: 1
*/
#define kOfxImageEffectPropMetadataSourceClip "OfxImageEffectPropMetadataSourceClip"

/** @brief The namespace prefix of the standard OFX metadata keys

Every one of the standard metadata keys defined in this file begins with this
prefix. It is reserved for the standard vocabulary: a host or plugin must not
define additional keys beginning with it, and must instead use a reverse DNS
name for anything not defined here.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyPrefixStandard "ofx/"

/** @brief The namespace prefix for keys carried verbatim from an OpenEXR file

Keys under this prefix hold the attributes of the OpenEXR file an image was read
from, named after the EXR attribute they came from, for example
``exr/chromaticities``. A host that cannot produce such keys may omit them; the
prefix is reserved, not mandatory.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyPrefixExr "exr/"

/** @brief The namespace prefix for keys carried verbatim from EXIF data

Keys under this prefix hold the EXIF tags found in the file an image was read
from, named after the EXIF tag they came from, for example ``exif/Make``. A host
that cannot produce such keys may omit them; the prefix is reserved, not
mandatory.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyPrefixExif "exif/"

/** @brief The namespace prefix for keys carried verbatim from a DPX file

Keys under this prefix hold the header fields of the DPX file an image was read
from, named after the DPX field they came from. A host that cannot produce such
keys may omit them; the prefix is reserved, not mandatory.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyPrefixDpx "dpx/"

/** @brief The namespace prefix for keys carried verbatim from a Cineon file

Keys under this prefix hold the header fields of the Cineon file an image was
read from, named after the Cineon field they came from. A host that cannot
produce such keys may omit them; the prefix is reserved, not mandatory.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyPrefixCin "cin/"

/** @brief The namespace prefix for keys carried verbatim from a TIFF file

Keys under this prefix hold the tags of the TIFF file an image was read from,
named after the TIFF tag they came from. A host that cannot produce such keys
may omit them; the prefix is reserved, not mandatory.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyPrefixTiff "tiff/"

/** @brief The namespace prefix for keys carried verbatim from a QuickTime file

Keys under this prefix hold the atoms and track metadata of the QuickTime file
an image was read from, named after the item they came from. A host that cannot
produce such keys may omit them; the prefix is reserved, not mandatory.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyPrefixQuickTime "quicktime/"

/** @brief The path of the file this image was read from

   - Type - string X 1

The value is a single, concrete, fully resolved path to an existing file, in the
native syntax of the host's filesystem. It is never a pattern: when the image
comes from a numbered image sequence the value is the path of the one file that
holds this image, with the frame number already substituted, for example
``/shots/ab_010/plate/ab_010_plate.0087.exr``. It is not
``ab_010_plate.%04d.exr``, ``ab_010_plate.####.exr`` or any other sequence
notation.

This follows from metadata being a property of a single image rather than of a
clip, and it means a plugin can open the value directly without having to parse
a host-specific pattern syntax. A plugin that needs the pattern must derive it
itself, using \ref kOfxMetadataKeySourceFrame to identify the varying part.

When the image comes from a container holding several images, such as a movie
file, the value is the path of that container, and every image read from it
carries the same value.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyFilePath "ofx/filepath"

/** @brief The number identifying this image within the source it was read from

   - Type - int X 1

For a numbered image sequence this is the frame number that appears in the name
of the file given by \ref kOfxMetadataKeyFilePath, so the two keys agree: the
value is the number that was substituted into the sequence pattern to produce
that path.

For a container holding several images, such as a movie file, this is the index
of the image within the container, counting from 0 for the first image stored in
it.

For a single-image file that is not part of a sequence, the key is omitted.

This is the frame number in the source's own numbering. It is unrelated to the
time at which the effect is being rendered, which the plugin already has, and a
host must not renumber it to match the timeline.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeySourceFrame "ofx/frame"

/** @brief The SMPTE timecode of this image

   - Type - string X 1

The value is formatted as ``HH:MM:SS:FF`` for non drop frame timecode and
``HH:MM:SS;FF`` for drop frame timecode, where the separator before the frames
field is the only thing that distinguishes the two. Each field is written with
exactly two digits, zero padded, and ``HH`` is in the range 00 to 23.

The frames field counts whole frames at the rate given by
\ref kOfxMetadataKeyFrameRate, so a plugin needs that key to convert a timecode
to a time.

The value is the timecode the source records for this image. It is not the
position of the image on the host's timeline: a host publishes the timecode
carried by the source rather than one it has computed from where the clip sits
in a project.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyTimecode "ofx/timecode"

/** @brief The film edge code of this image

   - Type - string X 1

The value is the edge code exactly as recorded by the source, for example a
KeyKode string, carried through unparsed and with no normalisation of its
spacing or punctuation.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyEdgecode "ofx/edgecode"

/** @brief The last modification time of the file this image was read from

   - Type - double X 1

The value is in seconds since the Unix epoch, that is since
1970-01-01T00:00:00Z, and is always expressed in UTC rather than in any local
time zone. It may carry a fractional part if the host knows the time to
sub-second precision; a host that only knows the time to the second publishes a
whole number of seconds. Times before the epoch are negative.

The file described is the one named by \ref kOfxMetadataKeyFilePath, so for a
numbered image sequence this is the modification time of the single file holding
this image, not of the sequence as a whole.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyMTime "ofx/mtime"

/** @brief The size of the file this image was read from

   - Type - double X 1

The value is the length in bytes of the file named by
\ref kOfxMetadataKeyFilePath, so for a numbered image sequence it is the size of
the single file holding this image, not the total size of the sequence.

It is a whole number, carried as a double because file sizes routinely exceed
the range of an int. A double represents whole numbers exactly up to 2^53, which
is over eight petabytes, so the value is exact for any file a host will
encounter.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyFileSize "ofx/filesize"

/** @brief The pixel aspect ratio recorded by the source

   - Type - double X 1

The value is the width of a pixel divided by its height, so 1.0 for square
pixels and 2.0 for pixels twice as wide as they are tall.

This is what the source file declares. It is not necessarily the pixel aspect
ratio of the image the plugin is given, which is
\ref kOfxImagePropPixelAspectRatio and which the host may have changed.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyPixelAspect "ofx/pixelaspect"

/** @brief The frame rate recorded by the source

   - Type - double X 1

The value is in frames per second, for example 24.0, or 23.976023976023978 for
24000/1001. It is what the source file declares, not necessarily the frame rate
of the clip the plugin is connected to.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyFrameRate "ofx/framerate"

/** @brief The shutter angle this image was exposed with

   - Type - double X 1

The value is in degrees, on the usual rotary shutter scale where 360.0 means the
shutter was open for the whole of the frame's duration and 180.0 means it was
open for half of it. The exposure in seconds is the angle divided by 360 and by
\ref kOfxMetadataKeyFrameRate.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyShutterAngle "ofx/shutterangle"

/** @brief The width of this image as stored in the source

   - Type - int X 1

The value is a whole number of pixels, and is the width of the image as the
source file stores it, before any cropping, scaling or proxying the host may
have applied on the way to the plugin.

Where a format distinguishes a display or format window from a data window that
may be larger or smaller, the value is the width of the display window, that
being the picture size the source declares and the one that
\ref kOfxMetadataKeyPixelAspect applies to.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyWidth "ofx/width"

/** @brief The height of this image as stored in the source

   - Type - int X 1

The value is a whole number of pixels, and is the height of the image as the
source file stores it, before any cropping, scaling or proxying the host may
have applied on the way to the plugin.

Where a format distinguishes a display or format window from a data window that
may be larger or smaller, the value is the height of the display window, that
being the picture size the source declares and the one that
\ref kOfxMetadataKeyPixelAspect applies to.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyHeight "ofx/height"

/** @brief The bit depth this image is stored at in the source

   - Type - int X 1

The value is the number of bits used for one component of one pixel in the
source file, so 8, 10, 12 or 16 for integer encodings, 16 for half float and 32
for single precision float. Where a format stores different components at
different widths, the value is the width of the widest component.

This describes the source, not the pixels handed to the plugin, whose depth is
given by \ref kOfxImageEffectPropPixelDepth. This key alone does not distinguish
a numeric format at a given bit depth, for example 16-bit half float from 16-bit
integer; \ref kOfxMetadataKeySampleType carries that distinction.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyBitDepth "ofx/bitdepth"

/** @brief The numeric format of the source's samples

   - Type - string X 1

The value is one of exactly three strings, always lowercase:

   - ``"uint"`` for unsigned integer samples
   - ``"int"`` for signed integer samples
   - ``"float"`` for IEEE floating-point samples

\ref kOfxMetadataKeyBitDepth alone cannot distinguish these: 16-bit half float
and 16-bit integer both report a bit depth of 16, and a signed 16-bit integer
source, such as a TIFF file with a signed ``SampleFormat``, reports the same bit
depth as an unsigned one. This key supplies the numeric format that
\ref kOfxMetadataKeyBitDepth omits, and the two keys are meant to be read
together.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeySampleType "ofx/sampletype"

/** @brief The person, organisation or application that created the source

   - Type - string X 1

The value is free text, carried through from the source unchanged, for example
the name of the application that wrote the file or of the artist credited in it.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyCreator "ofx/creator"

/** @brief The name of the production or project the source belongs to

   - Type - string X 1

The value is free text, carried through from the source unchanged.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyProject "ofx/project"

/** @brief The copyright notice attached to the source

   - Type - string X 1

The value is free text, carried through from the source unchanged.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyCopyright "ofx/copyright"

/** @brief A human readable comment attached to the source

   - Type - string X 1

The value is free text, carried through from the source unchanged. It may
contain newlines. A plugin must not attempt to parse it: it is meant to be shown
to a person.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyComment "ofx/comment"

/** @brief The focal length of the lens this image was shot with

   - Type - double X 1

The value is in millimetres, and is the actual focal length of the lens rather
than a focal length scaled to any reference format.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyFocalLength "ofx/focallength"

/** @brief The f-number of the lens this image was shot with

   - Type - double X 1

The value is the dimensionless ratio of the focal length to the diameter of the
entrance pupil, so 2.8 denotes f/2.8. It is not an aperture in stops and not a
T-stop.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyFNumber "ofx/fnumber"

/** @brief The exposure time this image was shot with

   - Type - double X 1

The value is in seconds, so a 1/48 second exposure is 0.020833333333333332. It
is not a reciprocal, not a fraction in a string, and not a shutter angle; the
shutter angle, where known, is \ref kOfxMetadataKeyShutterAngle.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyExposureTime "ofx/exposuretime"

/** @brief The slate information recorded with the source

   - Type - string X 1

The value is free text as recorded on the slate or in the camera's equivalent
field, for example a scene and take identifier, carried through from the source
unchanged and unparsed.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeySlateInfo "ofx/slateinfo"

/** @brief The names of the views present in the source this image was read from

   - Type - string X N

Each element is the name of one view, for example ``left`` and ``right``, in the
order the source stores them.

This key is explicitly not required. A host need not populate it, whether or not
it supports multiple views, and a plugin must not depend on it being present or
treat its absence as meaning the source is monoscopic. A plugin that needs to
know about views must use the multi-view mechanisms of the API rather than this
key, which exists so that a host that does happen to know the source's view
names has a standard place to publish them.

 @version Added in OpenFX NEXT
*/
#define kOfxMetadataKeyViewNames "ofx/viewnames"

/** @brief Callback used by OfxMetadataSuiteV1::metadataEnumerate to visit each key in a metadata property set

 \arg \c key       the name of a metadata key present in the property set being enumerated
 \arg \c userData  the opaque pointer passed to metadataEnumerate by the caller

 The host calls this function once for each key present in the metadata property set.

 The callback should return ::kOfxStatOK to have enumeration continue with the next key.
 Any other return value stops enumeration immediately, and that same status is
 returned to the caller of metadataEnumerate.

 No ordering of keys is guaranteed, and the host is not required to enumerate keys
 in the same order between separate calls, even for the same metadata handle, so a
 plugin must not infer any positional or stable ordering from a particular host's
 observed behaviour.
 */
typedef OfxStatus (OfxMetadataEnumerateFuncV1)(const char *key, void *userData);

/** @brief OFX suite that allows an effect to retrieve metadata associated with a clip's images.

 Metadata is exposed as a flat property set of int, double and string properties
 (and arrays thereof); it is a property of a particular image, i.e. of a clip at a
 given time, which is why clipGetMetadata takes a time but imageGetMetadata does not,
 since an image handle already denotes a clip at a specific time.

 Hosts may evaluate metadata lazily, for example only reading it from a file the
 first time it is requested for a given clip and time.
 */
typedef struct OfxMetadataSuiteV1 {
	/** @brief Retrieves the metadata property set for a clip at the given time

	 \arg \c clip      clip to retrieve metadata from
	 \arg \c time      time to retrieve metadata at
	 \arg \c metadata  filled with a handle to the retrieved metadata property set

	 The returned handle must be disposed of with metadataRelease once the plugin
	 is finished with it.

	 \pre
	 - clip was returned by clipGetHandle

	 \post
	 - metadata handle to be disposed of by metadataRelease

	 @returns
	 - ::kOfxStatOK - the metadata was successfully fetched and returned in the handle,
	 - ::kOfxStatReplyDefault - the clip has no metadata associated with it at the given time,
	 - ::kOfxStatErrBadHandle - the clip handle was invalid,
	 - ::kOfxStatErrMemory - the host had not enough memory to complete the operation, plugin should abort whatever it was doing.
	 */
	OfxStatus (*clipGetMetadata)(OfxImageClipHandle clip, OfxTime time, OfxPropertySetHandle *metadata);

	/** @brief Retrieves the metadata property set for an already-fetched image

	 \arg \c image     image handle, as returned by OfxImageEffectSuiteV1::clipGetImage
	 \arg \c metadata  filled with a handle to the retrieved metadata property set

	 Since an image handle already denotes a clip at a specific time, no time
	 parameter is required.

	 The returned handle must be disposed of with metadataRelease once the plugin
	 is finished with it.

	 \pre
	 - image was returned by OfxImageEffectSuiteV1::clipGetImage

	 \post
	 - metadata handle to be disposed of by metadataRelease

	 @returns
	 - ::kOfxStatOK - the metadata was successfully fetched and returned in the handle,
	 - ::kOfxStatReplyDefault - the image has no metadata associated with it,
	 - ::kOfxStatErrBadHandle - the image handle was invalid,
	 - ::kOfxStatErrMemory - the host had not enough memory to complete the operation, plugin should abort whatever it was doing.
	 */
	OfxStatus (*imageGetMetadata)(OfxPropertySetHandle image, OfxPropertySetHandle *metadata);

	/** @brief Releases a metadata handle previously returned by clipGetMetadata or imageGetMetadata

	 \arg \c metadata  metadata handle to release

	 \pre
	 - metadata was returned by clipGetMetadata or imageGetMetadata

	 \post
	 - all operations on metadata will be invalid

	 @returns
	 - ::kOfxStatOK - the metadata handle was successfully released,
	 - ::kOfxStatErrBadHandle - the metadata handle was invalid.
	 */
	OfxStatus (*metadataRelease)(OfxPropertySetHandle metadata);

	/** @brief Enumerates the keys present in a metadata property set

	 \arg \c metadata  metadata handle to enumerate the keys of
	 \arg \c callback  function called once per key present in metadata
	 \arg \c userData  opaque pointer passed unchanged to each call of callback

	 The host calls callback once for every key present in metadata, passing the
	 key name and userData. Enumeration stops as soon as callback returns a status
	 other than ::kOfxStatOK, and that status becomes this call's return value.

	 No ordering of keys is guaranteed, and the order need not be stable between
	 separate calls, even for the same metadata handle, so a plugin must not rely
	 on a particular host's observed ordering.

	 Once a key name has been obtained this way, the plugin can retrieve its value
	 from metadata using the generic Property Suite.

	 \pre
	 - metadata was returned by clipGetMetadata or imageGetMetadata

	 @returns
	 - ::kOfxStatOK - enumeration completed, having visited every key,
	 - ::kOfxStatErrBadHandle - the metadata handle was invalid,
	 - any other status returned by callback to stop enumeration early.
	 */
	OfxStatus (*metadataEnumerate)(OfxPropertySetHandle metadata, OfxMetadataEnumerateFuncV1 callback, void *userData);

} OfxMetadataSuiteV1;

#ifdef __cplusplus
}
#endif

#endif
