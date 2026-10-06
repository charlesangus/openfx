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
API for reading the host-defined metadata attached to a clip's images.

Metadata is a flat property set. Each key is a string, and its value is an int,
a double or a string, or an array of one of those; there is no nesting and no
binary blob type.

A key is a string chosen by whoever publishes it. This suite defines no keys and
no namespaces and puts no requirement on the content of the metadata: a host
publishes what it has, under whatever names it uses. A plugin that consumes a
key should let the user name it.

@version Added in OpenFX NEXT
*/


/** @brief the string that names the MetadataSuite, passed to OfxHost::fetchSuite */
#define kOfxMetadataSuite "OfxMetadataSuite"

/** @brief The value type of a metadata key, as reported to OfxMetadataEnumerateFuncV1

 Every key present in a metadata property set has exactly one of these types.

 @version Added in OpenFX NEXT
 */
typedef enum OfxMetadataValueType
{
	/** @brief The key's value is fetched with OfxPropertySuiteV1::propGetInt or propGetIntN */
	kOfxMetadataValueTypeInteger = 1,

	/** @brief The key's value is fetched with OfxPropertySuiteV1::propGetDouble or propGetDoubleN */
	kOfxMetadataValueTypeDouble = 2,

	/** @brief The key's value is fetched with OfxPropertySuiteV1::propGetString or propGetStringN */
	kOfxMetadataValueTypeString = 3
} OfxMetadataValueType;

/** @brief Callback used by OfxMetadataSuiteV1::metadataEnumerate to visit each key in a metadata property set

 \arg \c key       the name of a metadata key present in the property set being enumerated
 \arg \c type      the value type of the key
 \arg \c dimension the number of values the key holds, 1 for a scalar key
 \arg \c userData  the opaque pointer passed to metadataEnumerate by the caller

 The host calls this function once for each key present in the metadata property set, in
 no guaranteed order. The callback returns ::kOfxStatOK to have enumeration continue with
 the next key; any other return value stops enumeration immediately, and that same status
 is returned to the caller of metadataEnumerate.
 */
typedef OfxStatus (OfxMetadataEnumerateFuncV1)(const char *key, OfxMetadataValueType type,
                                                int dimension, void *userData);

/** @brief OFX suite that lets an effect read the metadata of a clip's images.

 Metadata is a property of a particular image, that is of a clip at a given time, so
 clipGetMetadata takes a time while imageGetMetadata, whose image handle already denotes a
 clip at a specific time, does not. Hosts may evaluate metadata lazily.

 The sets returned by clipGetMetadata and imageGetMetadata are read-only.
 */
typedef struct OfxMetadataSuiteV1 {
	/** @brief Retrieves the metadata property set for a clip at the given time

	 \arg \c clip      clip to retrieve metadata from
	 \arg \c time      time to retrieve metadata at
	 \arg \c metadata  filled with a handle to the retrieved metadata property set

	 \pre
	 - clip was returned by clipGetHandle

	 \post
	 - on ::kOfxStatOK, metadata is a handle to a read-only property set, possibly empty, to be
	   disposed of by metadataRelease
	 - on any other status code, metadata is set to NULL and there is nothing to release

	 @returns
	 - ::kOfxStatOK - the metadata was successfully fetched and returned in the handle, which is
	   empty if the clip has no metadata associated with it at the given time,
	 - ::kOfxStatErrBadHandle - the clip handle was invalid,
	 - ::kOfxStatErrMemory - the host had not enough memory to complete the operation, plugin
	   should abort whatever it was doing.,
	 - ::kOfxStatFailed - something went wrong but no error code is appropriate, the plugin
	   should post a message.
	 */
	OfxStatus (*clipGetMetadata)(OfxImageClipHandle clip, OfxTime time, OfxPropertySetHandle *metadata);

	/** @brief Retrieves the metadata property set for an already-fetched image

	 \arg \c image     image handle, as returned by OfxImageEffectSuiteV1::clipGetImage
	 \arg \c metadata  filled with a handle to the retrieved metadata property set

	 \pre
	 - image was returned by OfxImageEffectSuiteV1::clipGetImage

	 \post
	 - on ::kOfxStatOK, metadata is a handle to a read-only property set, possibly empty, to be
	   disposed of by metadataRelease
	 - on any other status code, metadata is set to NULL and there is nothing to release

	 @returns
	 - ::kOfxStatOK - the metadata was successfully fetched and returned in the handle, which is
	   empty if the image has no metadata associated with it,
	 - ::kOfxStatErrBadHandle - the image handle was invalid,
	 - ::kOfxStatErrMemory - the host had not enough memory to complete the operation, plugin
	   should abort whatever it was doing.,
	 - ::kOfxStatFailed - something went wrong but no error code is appropriate, the plugin
	   should post a message.
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
	 \arg \c callback  function called once per key present in metadata, with that key's type and dimension
	 \arg \c userData  opaque pointer passed unchanged to each call of callback

	 The host calls callback once for every key present in metadata, passing the key name,
	 its value type and dimension, and userData. Enumeration stops as soon as callback
	 returns a status other than ::kOfxStatOK, and that status becomes this call's return
	 value. Once a key's name, type and dimension are known, its value is read from metadata
	 with the generic Property Suite.

	 No ordering of keys is guaranteed, and the order need not be stable between separate
	 calls, even for the same metadata handle.

	 \pre
	 - metadata was returned by clipGetMetadata or imageGetMetadata

	 @returns
	 - ::kOfxStatOK - enumeration completed, having visited every key,
	 - ::kOfxStatErrBadHandle - the metadata handle was invalid,
	 - ::kOfxStatErrValue - callback is NULL,
	 - ::kOfxStatFailed - something went wrong but no error code is appropriate, the plugin
	   should post a message,
	 - any other status returned by callback to stop enumeration early.
	 */
	OfxStatus (*metadataEnumerate)(OfxPropertySetHandle metadata,
	                                OfxMetadataEnumerateFuncV1 callback, void *userData);
} OfxMetadataSuiteV1;

#ifdef __cplusplus
}
#endif

#endif
