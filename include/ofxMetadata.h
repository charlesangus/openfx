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
API for reading the host-defined metadata attached to a clip's
images, and for an effect to contribute metadata of its own through
\ref kOfxImageEffectActionGetMetadata.

Metadata is a flat property set. Each key is a string, and its value is an int,
a double or a string, or an array of one of those; there is no nesting and no
binary blob type.

A key is a string chosen by whoever publishes it. This suite defines no keys and
no namespaces and puts no requirement on the content of the metadata: a host
publishes what it has, under whatever names it uses. A plugin that consumes a
key should let the user name it, or expect an upstream effect to place the value
there.

@version Added in OpenFX NEXT
*/


/** @brief the string that names the MetadataSuite, passed to OfxHost::fetchSuite */
#define kOfxMetadataSuite "OfxMetadataSuite"

/** @brief Action called to retrieve the metadata an effect contributes for a clip at a given time.

Metadata is a property of an image, that is of a clip at a specific time, so this action is
always time-parameterised. Its result for a given time is valid only while the input metadata it
was composed from, the effect's parameter values and the effect's clip connections remain
unchanged; the host must re-issue the action after any of those change.

An effect writes the metadata it contributes into the set passed in
\ref kOfxImageEffectPropMetadataSet, or contributes nothing. That set arrives empty and is not
pre-populated with the metadata inherited from the effect's input clips; an effect that needs to
see what its inputs carry reads it with OfxMetadataSuiteV1::clipGetMetadata.

An effect that does not trap the action returns \ref kOfxStatReplyDefault, and the host then
ignores everything it wrote: it reads back neither the contributed set nor either inheritance
control in outArgs, and composes the output's metadata from the defaults it initialised
outArgs with. An effect that means anything it wrote to be honoured must return ::kOfxStatOK. The
output's metadata is then the inherited keys with the contributed keys written over them: a key the
effect writes replaces the inherited value of the same key.

 @param handle handle to the instance, cast to an \ref OfxImageEffectHandle

 @param inArgs has the following properties
     - \ref kOfxPropTime the time at which the metadata is being requested
     - \ref kOfxImageEffectPropMetadataSet the metadata property set the effect writes the metadata
       it contributes into

 @param outArgs is a property set describing how metadata is inherited from the effect's input
 clips, with the following properties
     - \ref kOfxImageEffectPropMetadataSourceClip the ordered list of input clip names whose
       metadata the output composes
     - a set of char * X N properties, one for each input clip the effect describes, connected
       or not, labelled with \c OfxImageClipPropMetadataRetainedKeys_ post pended with the
       clip's name, for example \c OfxImageClipPropMetadataRetainedKeys_Source. Each such
       property lists the metadata keys retained from that input clip; a key absent from the
       list is not carried through from that clip. Before the action is called the host
       initialises the list for the clip named by the default value of
       \ref kOfxImageEffectPropMetadataSourceClip to the full set of keys present on that clip,
       and to the empty list for every other input clip. An input clip that is not connected
       contributes nothing to the composition, even when the list names it.

 @returns
     - \ref kOfxStatOK the action was trapped and the host honours the metadata set and the
       outArgs the effect wrote,
     - \ref kOfxStatReplyDefault the action was not trapped, so the host uses its default metadata
       and discards everything the effect wrote,
     - \ref kOfxStatErrMemory the host ran out of memory, in which case the action may be
       called again after a memory purge,
     - \ref kOfxStatFailed something went wrong but no error code is appropriate, the
       plugin should post a message,
     - \ref kOfxStatErrFatal

 @version Added in OpenFX NEXT

    @actiondef
    inArgs:
      - OfxPropTime
      - OfxImageEffectPropMetadataSet
    outArgs:
      - OfxImageEffectPropMetadataSourceClip
    # this special prop has the clip name postpended after "_"
    # - OfxImageClipPropMetadataRetainedKeys_
 */
#define kOfxImageEffectActionGetMetadata "OfxImageEffectActionGetMetadata"

/** @brief The metadata property set an effect writes its metadata contribution into

The host passes this in the inArgs of \ref kOfxImageEffectActionGetMetadata. The value is a
pointer holding an \ref OfxPropertySetHandle, which the effect casts to before use.

The set arrives empty and is the only metadata property set an effect may write to. Keys are
added with the \c metadataSet entry points of \ref OfxMetadataSuiteV1, which create a key that
is not already present; the generic Property Suite cannot create a key, but once a key has been
written its value can be read back through it, using the type and dimension that
OfxMetadataSuiteV1::metadataEnumerate reports. Enumeration is permitted on this set.

The handle is owned by the host and is valid only for the duration of the action. It must not be
released with OfxMetadataSuiteV1::metadataRelease.

   - Type - pointer X 1
   - Property Set - inArgs property set of the \ref kOfxImageEffectActionGetMetadata action
   - Valid Values - a handle to a writable metadata property set, supplied by the host

 @version Added in OpenFX NEXT

   @propdef
   type: pointer
   dimension: 1
*/
#define kOfxImageEffectPropMetadataSet "OfxImageEffectPropMetadataSet"

/** @brief The ordered list of input clip names whose metadata the output clip inherits

An effect sets this in the outArgs of \ref kOfxImageEffectActionGetMetadata to nominate the
input clips the output clip's metadata is composed from. Each named clip contributes the keys
selected by its \c OfxImageClipPropMetadataRetainedKeys_ property, described under that action.

The list is read in increasing precedence: where two named clips carry the same key, the value
from the later entry wins. An empty list means the output inherits no metadata from any clip. A
name that does not match any of the effect's input clips is ignored, as if it were absent.

   - Type - string X N
   - Property Set - outArgs property set of the \ref kOfxImageEffectActionGetMetadata action
   - Valid Values - the name of any of the effect's input clips, each may appear at most once and
                    in any order; the empty list is valid and means no metadata is inherited
   - Default - a single-element list naming the first connected input clip in the order the
               effect described them, or the empty list if no input clip is connected

 @version Added in OpenFX NEXT

   @propdef
   type: string
   dimension: N
*/
#define kOfxImageEffectPropMetadataSourceClip "OfxImageEffectPropMetadataSourceClip"

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

/** @brief OFX suite that lets an effect read the metadata of a clip's images and write the
    metadata it contributes.

 Metadata is a property of a particular image, that is of a clip at a given time, so
 clipGetMetadata takes a time while imageGetMetadata, whose image handle already denotes a
 clip at a specific time, does not. Hosts may evaluate metadata lazily.

 The sets returned by clipGetMetadata and imageGetMetadata are read-only. The only writable
 set is the one passed to \ref kOfxImageEffectActionGetMetadata in
 \ref kOfxImageEffectPropMetadataSet, and keys are created in it only with the six metadataSet
 entry points below, never through the generic Property Suite. The N forms take a count of at
 least 1 and an array of that many values, which the host copies, and no index; the scalar
 forms are exactly the N forms with a count of 1. Either form creates the key if it is absent,
 and replaces both the value and the dimension of a key that is already present, so that on
 ::kOfxStatOK the key holds exactly the values given, with count as its dimension. The key
 must be NULL terminated.

 All six share one set of status codes:

 - ::kOfxStatOK - the key was written, having been created if it was not already present,
 - ::kOfxStatErrBadHandle - metadata is not a metadata property set, or key is NULL,
 - ::kOfxStatErrValue - metadata is read-only, key is empty, count is less than 1, values is
   NULL or a string among the values is NULL. A write which fails leaves the key as it was,
   present or absent,
 - ::kOfxStatErrMemory - the host had not enough memory to complete the operation, the
   plugin should abort whatever it was doing.
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
	 - ::kOfxStatErrBadHandle - the metadata handle was invalid,
	 - ::kOfxStatErrValue - the metadata handle is not the plugin's to release, being one the
	   host passed to an action.
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
	 - metadata was returned by clipGetMetadata or imageGetMetadata, or is the set passed to
	   the \ref kOfxImageEffectActionGetMetadata action in \ref kOfxImageEffectPropMetadataSet

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

	/** @brief Writes a single string value to a metadata key, creating the key if needed

	 \arg \c metadata  writable metadata property set to write into
	 \arg \c key       name of the metadata key to write
	 \arg \c value     value to give the key

	 Exactly metadataSetStringN with a count of 1.

	 @returns the status codes shared by the metadataSet entry points, described in this
	 suite's documentation above.
	 */
	OfxStatus (*metadataSetString) (OfxPropertySetHandle metadata, const char *key, const char *value);

	/** @brief Writes a single double value to a metadata key, creating the key if needed

	 \arg \c metadata  writable metadata property set to write into
	 \arg \c key       name of the metadata key to write
	 \arg \c value     value to give the key

	 Exactly metadataSetDoubleN with a count of 1.

	 @returns the status codes shared by the metadataSet entry points, described in this
	 suite's documentation above.
	 */
	OfxStatus (*metadataSetDouble) (OfxPropertySetHandle metadata, const char *key, double value);

	/** @brief Writes a single int value to a metadata key, creating the key if needed

	 \arg \c metadata  writable metadata property set to write into
	 \arg \c key       name of the metadata key to write
	 \arg \c value     value to give the key

	 Exactly metadataSetIntN with a count of 1.

	 @returns the status codes shared by the metadataSet entry points, described in this
	 suite's documentation above.
	 */
	OfxStatus (*metadataSetInt)    (OfxPropertySetHandle metadata, const char *key, int value);

	/** @brief Writes an array of string values to a metadata key, creating the key if needed

	 \arg \c metadata  writable metadata property set to write into
	 \arg \c key       name of the metadata key to write
	 \arg \c count     number of values being written, which becomes the key's dimension
	 \arg \c values    array of count values to give the key

	 @returns the status codes shared by the metadataSet entry points, described in this
	 suite's documentation above.
	 */
	OfxStatus (*metadataSetStringN)(OfxPropertySetHandle metadata, const char *key,
	                                 int count, const char *const*values);

	/** @brief Writes an array of double values to a metadata key, creating the key if needed

	 \arg \c metadata  writable metadata property set to write into
	 \arg \c key       name of the metadata key to write
	 \arg \c count     number of values being written, which becomes the key's dimension
	 \arg \c values    array of count values to give the key

	 @returns the status codes shared by the metadataSet entry points, described in this
	 suite's documentation above.
	 */
	OfxStatus (*metadataSetDoubleN)(OfxPropertySetHandle metadata, const char *key,
	                                 int count, const double *values);

	/** @brief Writes an array of int values to a metadata key, creating the key if needed

	 \arg \c metadata  writable metadata property set to write into
	 \arg \c key       name of the metadata key to write
	 \arg \c count     number of values being written, which becomes the key's dimension
	 \arg \c values    array of count values to give the key

	 @returns the status codes shared by the metadataSet entry points, described in this
	 suite's documentation above.
	 */
	OfxStatus (*metadataSetIntN)   (OfxPropertySetHandle metadata, const char *key,
	                                 int count, const int *values);

} OfxMetadataSuiteV1;

#ifdef __cplusplus
}
#endif

#endif
