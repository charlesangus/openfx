.. SPDX-License-Identifier: CC-BY-4.0
.. _imageMetadata:

Clip and Image Metadata
=======================

Metadata is the descriptive information that travels with an image but is not
part of its pixels: where the image was read from, when the file was written,
what lens shot it, what the timecode was. A host exposes it to plugins through
the :ref:`OfxMetadataSuiteV1`, fetched under the name
:c:macro:`kOfxMetadataSuite`. The suite is documented from the header on the
:ref:`suite reference page <OfxMetadataSuiteV1>`; this chapter describes the
model it implements. What a set contains is up to the host: the suite defines
a mechanism for reading metadata, not a schema.

Metadata Belongs to an Image
----------------------------

Metadata is a property of an image, that is of a clip at a particular time,
rather than of a clip as a whole. Two frames of the same clip may carry
different metadata, and usually do: the file path and the timecode of a
numbered image sequence change from frame to frame. This is why the suite's
``clipGetMetadata`` takes a time, while ``imageGetMetadata``, whose image
handle already denotes a clip at a specific time, does not.

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

Key Names and Content
---------------------

Metadata content is host-defined. The suite defines no keys, no namespaces and
no content rules: a host decides which keys it publishes, under what names,
with what types and units, and whether it publishes a given piece of
information at all. Two hosts reading the same file may publish different
keys for it, and a plugin must not assume that a key it knows from one host
exists on another. A key is any string.

A plugin that consumes a value therefore cannot hard-code where to find it.
It lets the user name the key to read, typically through a string
parameter. A host should document the keys it publishes.
