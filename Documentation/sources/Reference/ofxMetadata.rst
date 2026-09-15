.. SPDX-License-Identifier: CC-BY-4.0
.. _imageMetadata:

Clip and Image Metadata
=======================

Metadata is the descriptive information that travels with an image but is not
part of its pixels: where the image was read from, when the file was written,
what lens shot it, what the timecode was. A host exposes it to plugins through
the :ref:`OfxMetadataSuiteV1`, fetched under the name
:c:macro:`kOfxMetadataSuite`, and a plugin contributes metadata of its own,
and controls what its output inherits from its inputs, in the
:c:macro:`kOfxImageEffectActionGetMetadata` action. The suite, the action's
two properties and the standard key vocabulary are documented from the header
on the :ref:`suite reference page <OfxMetadataSuiteV1>`; this chapter
describes the model they implement.

Metadata Belongs to an Image
----------------------------

Metadata is a property of an image, that is of a clip at a particular time,
rather than of a clip as a whole. Two frames of the same clip may carry
different metadata, and usually do: the file path and the timecode of a
numbered image sequence change from frame to frame. This is why the suite's
``clipGetMetadata`` takes a time, while ``imageGetMetadata``, whose image
handle already denotes a clip at a specific time, does not, and why the
action is always time-parameterised.

Structurally, a metadata set is a flat property set. Each entry has a string
key and a value that is an int, a double or a string, or an array of one of
those, described by a type from :cpp:type:`OfxMetadataValueType` and a
dimension. There is no nesting and no binary blob type.

Reading Metadata
----------------

The sets returned by ``clipGetMetadata`` and ``imageGetMetadata`` are
read-only, and belong to the plugin until it disposes of them with
``metadataRelease``. An image that has no metadata is not an error: the call
succeeds and the set it returns is empty. A host may evaluate metadata
lazily.

The keys present in a set are discovered with ``metadataEnumerate``, which
calls an :cpp:type:`OfxMetadataEnumerateFuncV1` once per key with the key's
name, its type and its dimension, in no guaranteed order. Once those three
are known the value is read with the generic Property Suite, using the
``propGet`` entry point that the type names and the array form when the
dimension is more than 1.

Contributing Metadata
---------------------

An effect that traps :c:macro:`kOfxImageEffectActionGetMetadata` writes the
keys it contributes into the set the host passes in
:c:macro:`kOfxImageEffectPropMetadataSet`. That set arrives empty: it is not
pre-populated with the metadata inherited from the effect's input clips, and
an effect that needs to see what its inputs carry reads it separately with
``clipGetMetadata``.

Keys are created in the contributed set only with the six ``metadataSet``
entry points of the suite, three scalar and three array forms, one pair per
value type. Either form creates a key that is absent and replaces the value
and dimension of one that is present, so there is no way to append to an
existing key, and there is no entry point to delete one. The generic Property
Suite cannot create a key in this set, though once a key has been written its
value can be read back through it, and the set may be enumerated. The handle
is owned by the host, is valid only for the duration of the action, and must
not be passed to ``metadataRelease``.

The action's result is honoured only when the effect returns
:c:macro:`kOfxStatOK`. An effect that does not trap the action, or that
returns :c:macro:`kOfxStatReplyDefault`, has everything it wrote discarded:
the host reads back neither the contributed set nor the inheritance controls
in ``outArgs``, and composes the output's metadata from the defaults it
initialised ``outArgs`` with.

A key a plugin contributes is subject to the same namespace rules as any
other, described under `The Key Vocabulary`_: it is a vendor key, named in
reverse DNS form, and never a new key under
:c:macro:`kOfxMetadataKeyPrefixStandard`.

Composing Metadata Across Input Clips
-------------------------------------

Besides adding keys of its own, an effect trapping the action controls which
keys reach its output from its input clips, and which input wins where two
carry the same key. It does so with the ``outArgs`` properties the host
initialises before calling the action.

:c:macro:`kOfxImageEffectPropMetadataSourceClip` is the ordered list of input
clip names the output composes its metadata from. The list is read in
increasing precedence: where two named clips carry the same key, the value
from the later entry wins, so naming two clips in one order composes the
second over the first, and reversing the list reverses the outcome for every
key they disagree on. An empty list means the output inherits no metadata
from any clip, and a name that matches none of the effect's input clips is
ignored, as if it were absent.

Alongside it, ``outArgs`` carries one retained-keys property for each input
clip the effect describes, connected or not, named
``OfxImageClipPropMetadataRetainedKeys_`` post pended with the clip's name.
Each lists the keys retained from that clip; a key absent from a clip's list
is not carried through from that clip, whatever the source-clip list says.
This is also how an inherited key is suppressed, since nothing in the suite
removes a key: the effect leaves it out of the corresponding list.

The defaults follow the first connected input clip. Before the action is
called the host sets the source-clip list to a single entry naming the first
connected input clip in the order the effect described them, or to the empty
list if none is connected, fills that clip's retained-keys list with every
key present on it, and leaves every other clip's list empty. An effect that
touches neither property therefore inherits all of the first connected clip's
metadata and nothing from any other input. An input clip that is not
connected contributes nothing to the composition, even when the source-clip
list names it.

The keys the effect contributes in :c:macro:`kOfxImageEffectPropMetadataSet`
are written over the inherited keys, so a key the effect writes replaces the
inherited value of the same key.

The Key Vocabulary
------------------

Keys are strings, and the key space is divided into namespaces by a prefix
ending in a forward slash. Keys under :c:macro:`kOfxMetadataKeyPrefixStandard`
are the standard, host-independent vocabulary defined in the header, from
:c:macro:`kOfxMetadataKeyFilePath` to :c:macro:`kOfxMetadataKeyViewNames`,
each with its type, its units and its edge cases documented on the
:ref:`suite reference page <OfxMetadataSuiteV1>`; neither a host nor a
plugin may invent new keys in that namespace. Keys under one of the format
prefixes, :c:macro:`kOfxMetadataKeyPrefixExr` through
:c:macro:`kOfxMetadataKeyPrefixQuickTime`, are carried verbatim from the file
an image was read from; those prefixes are reserved, not mandatory, and a
host that cannot produce them may omit them entirely. Any other key is a
vendor key and must be named in reverse DNS form using a domain the definer
controls, whether the definer is a host or a plugin. A host publishes a
standard key only when it knows the value, omitting it rather than publishing
a placeholder, and publishes a value it does know under the standard key
rather than a vendor name.

When the Host Re-issues the Action
----------------------------------

The result of :c:macro:`kOfxImageEffectActionGetMetadata` for a given time is
valid only while the input metadata it was composed from, the effect's
parameter values and the effect's clip connections remain unchanged. The host
must re-issue the action after any of those change.
