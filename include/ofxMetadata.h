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
@version Added in OpenFX NEXT
*/


/** @brief the string that names the MetadataSuite, passed to OfxHost::fetchSuite */
#define kOfxMetadataSuite "OfxMetadataSuite"

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
