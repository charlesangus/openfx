.. SPDX-License-Identifier: CC-BY-4.0
.. _imageMetadata:

Clip and Image Metadata
=======================

Metadata is the descriptive information that travels with an image but is not
part of its pixels: where the image was read from, when the file was written,
what lens shot it, what the timecode was. A host exposes it to plugins through
the :c:macro:`kOfxMetadataSuite`, and a plugin can contribute to it or edit it
in the
:c:macro:`kOfxImageEffectActionGetMetadata`
action.

Metadata is a property of an image, that is of a clip at a particular time,
rather than of a clip as a whole. Two frames of the same clip may perfectly well
carry different metadata, and usually do: the file path and the timecode of a
numbered image sequence change from frame to frame.

Structurally, metadata is a flat property set. Each entry has a string key and a
value that is an int, a double or a string, or an array of one of those. There
is no nesting, and there is no binary blob type. Values are read with the
generic Property Suite once the keys are known, and the keys present on a given
image can be discovered with ``metadataEnumerate``.

Composing Metadata from Input Clips
------------------------------------

An effect trapping :c:macro:`kOfxImageEffectActionGetMetadata` does not merely add new keys of its
own: it also controls which keys reach the output clip from its input clips, and how conflicting
values between inputs are resolved. This is done with two ``outArgs`` properties.

Every input clip that is currently attached has its own retained-keys list, named
``OfxImageClipPropMetadataRetainedKeys_`` post pended with the clip's name, for example
``OfxImageClipPropMetadataRetainedKeys_Source`` or ``OfxImageClipPropMetadataRetainedKeys_Mask``.
Each list names the metadata keys that are retained from that particular clip; a key absent from
a clip's list is not carried through from that clip, no matter what
:c:macro:`kOfxImageEffectPropMetadataSourceClip` says.

:c:macro:`kOfxImageEffectPropMetadataSourceClip` is the ordered list of input clip names that the
output composes its metadata from. **The list is read in increasing precedence: where two of the
named clips carry the same key, the value from the clip named later in the list wins.** This is
what lets an effect express composition in either direction. To compose a clip named ``Mask``
over a clip named ``Source`` — that is, to let ``Mask``'s metadata override ``Source``'s wherever
the two disagree — an effect sets the list to::

    ["Source", "Mask"]

A key present on both clips takes ``Mask``'s value, because ``Mask`` comes later in the list. A
key present on only one of the two clips passes through unchanged, since there is nothing to
conflict with. Reversing the list to ``["Mask", "Source"]`` composes ``Source`` over ``Mask``
instead, and reverses the outcome for every key the two clips disagree on. Naming a clip only once
in the list, for example ``["Source"]``, inherits metadata from that clip alone, exactly as a
single fixed source clip would.

An **empty list** means the output inherits no metadata at all, from any input clip; this is the
one case that cannot be confused with any other reading of the list. A clip named in the list that
is not one of the effect's currently attached input clips is ignored, as though it had been left
out of the list.

Before the action is called, the host initialises :c:macro:`kOfxImageEffectPropMetadataSourceClip`
to a single-element list naming the first input clip the effect describes, and initialises that
clip's retained-keys list to every key present on the clip's images at the time being queried, with
every other input clip's retained-keys list left empty. An effect that does not touch either
property therefore inherits all of the first input clip's metadata and nothing from any other
input, unchanged from a plugin's point of view whether or not it is aware that composition across
several clips is possible.

The Key Namespaces
------------------

Metadata keys are strings, and the key space is divided into namespaces by a
prefix ending in a forward slash. Three kinds of key exist.

**Standard keys** begin with ``ofx/`` and are listed in
`The Standard Keys`_ below. This namespace is reserved: neither a host nor a
plugin may invent new keys in it. Adding a key to it is a change to this
specification.

**Format keys** begin with one of the reserved format prefixes and hold values
carried verbatim from the file an image was read from, under a name derived from
the name the format itself gives them. The reserved prefixes are

.. list-table::
    :header-rows: 1
    :widths: 20 40 40

    * - Prefix
      - Constant
      - Source
    * - ``exr/``
      - :c:macro:`kOfxMetadataKeyPrefixExr`
      - OpenEXR attributes, for example ``exr/chromaticities``
    * - ``exif/``
      - :c:macro:`kOfxMetadataKeyPrefixExif`
      - EXIF tags, for example ``exif/Make``
    * - ``dpx/``
      - :c:macro:`kOfxMetadataKeyPrefixDpx`
      - DPX header fields
    * - ``cin/``
      - :c:macro:`kOfxMetadataKeyPrefixCin`
      - Cineon header fields
    * - ``tiff/``
      - :c:macro:`kOfxMetadataKeyPrefixTiff`
      - TIFF tags
    * - ``quicktime/``
      - :c:macro:`kOfxMetadataKeyPrefixQuickTime`
      - QuickTime atoms and track metadata

These prefixes are reserved, not mandatory. Producing them requires a host to
have kept the raw container data around, and not every host is built that way, so
**a host that cannot produce format-prefixed keys may omit them entirely**. A
plugin must therefore treat all format keys as optional, and must not fail or
degrade badly when none are present. What the reservation buys is that a host
which *can* produce them will produce them under a predictable name, and that
nobody else will use those prefixes for anything else.

**Vendor keys** are everything else, and must be named in reverse DNS form using
a domain the definer controls, for example
``com.example.mytool.regionOfInterest``. This applies equally to keys defined by
a host and keys defined by a plugin. A key that is neither a standard key nor a
format key and is not in reverse DNS form is not a valid OFX metadata key.

When a Host Publishes a Key
---------------------------

A host publishes a standard key only when it knows the value. A value that is
not known must be **omitted** rather than published as an empty string, a zero
or any other placeholder, so that a plugin can tell "not available" apart from a
genuine value. Correspondingly, a plugin must check for the presence of every
key it uses and must behave sensibly when a key is missing.

Where a host does know the value of one of the standard keys, it should publish
it under that key rather than under a vendor-specific name of its own. The point
of the standard namespace is that the same information arrives under the same
name whichever host the plugin is running in.

The Standard Keys
-----------------

.. list-table::
    :header-rows: 1
    :widths: 22 26 12 40

    * - Key
      - Constant
      - Type
      - Meaning
    * - ``ofx/filepath``
      - :c:macro:`kOfxMetadataKeyFilePath`
      - string X 1
      - The fully resolved path of the file this image was read from, in the
        native syntax of the host's filesystem. Never a sequence pattern; see
        `Sequences and the File Path`_.
    * - ``ofx/frame``
      - :c:macro:`kOfxMetadataKeySourceFrame`
      - int X 1
      - The number identifying this image within its source: for a numbered
        sequence the frame number appearing in the resolved file path, for a
        container holding several images the index of the image within it,
        counting from 0. Omitted for a single-image file.
    * - ``ofx/timecode``
      - :c:macro:`kOfxMetadataKeyTimecode`
      - string X 1
      - SMPTE timecode as recorded by the source for this image, not its
        position on the host's timeline. Formatted ``HH:MM:SS:FF`` for non drop
        frame and ``HH:MM:SS;FF`` for drop frame. Every field is two
        zero-padded digits and ``HH`` runs from 00 to 23. The frames field
        counts whole frames at ``ofx/framerate``.
    * - ``ofx/edgecode``
      - :c:macro:`kOfxMetadataKeyEdgecode`
      - string X 1
      - The film edge code exactly as recorded by the source, unparsed and with
        its spacing and punctuation unchanged.
    * - ``ofx/mtime``
      - :c:macro:`kOfxMetadataKeyMTime`
      - double X 1
      - Last modification time of the file named by ``ofx/filepath``, in
        seconds since 1970-01-01T00:00:00Z, in UTC. May be fractional if the
        host knows the time to sub-second precision; negative before the epoch.
    * - ``ofx/filesize``
      - :c:macro:`kOfxMetadataKeyFileSize`
      - double X 1
      - Length in bytes of the file named by ``ofx/filepath``, as a whole
        number. A double rather than an int because file sizes routinely
        exceed an int's range; a double is exact for whole numbers up to
        2^53, which is over eight petabytes.
    * - ``ofx/pixelaspect``
      - :c:macro:`kOfxMetadataKeyPixelAspect`
      - double X 1
      - Pixel aspect ratio declared by the source, as pixel width divided by
        pixel height. Not necessarily the pixel aspect ratio of the image handed
        to the plugin.
    * - ``ofx/framerate``
      - :c:macro:`kOfxMetadataKeyFrameRate`
      - double X 1
      - Frame rate declared by the source, in frames per second. Not necessarily
        the frame rate of the clip the plugin is connected to.
    * - ``ofx/shutterangle``
      - :c:macro:`kOfxMetadataKeyShutterAngle`
      - double X 1
      - Shutter angle in degrees on the rotary shutter scale, where 360.0 is a
        shutter open for the whole frame duration and 180.0 for half of it.
    * - ``ofx/width``
      - :c:macro:`kOfxMetadataKeyWidth`
      - int X 1
      - Width in pixels of the image as stored in the source, before any
        cropping, scaling or proxying by the host. Where the format
        distinguishes a display window from a data window, the display window.
    * - ``ofx/height``
      - :c:macro:`kOfxMetadataKeyHeight`
      - int X 1
      - Height in pixels of the image as stored in the source, before any
        cropping, scaling or proxying by the host. Where the format
        distinguishes a display window from a data window, the display window.
    * - ``ofx/bitdepth``
      - :c:macro:`kOfxMetadataKeyBitDepth`
      - int X 1
      - Bits per component as stored in the source: 8, 10, 12 or 16 for integer
        encodings, 16 for half float, 32 for single precision float. Where
        components differ, the width of the widest. Describes the source, not
        the pixels handed to the plugin. Does not by itself distinguish a
        numeric format at a given bit depth; see ``ofx/sampletype``.
    * - ``ofx/sampletype``
      - :c:macro:`kOfxMetadataKeySampleType`
      - string X 1
      - The numeric format of the source's samples: ``"uint"`` for unsigned
        integer samples, ``"int"`` for signed integer samples, or ``"float"``
        for IEEE floating-point samples, always lowercase. Supplies the
        distinction ``ofx/bitdepth`` alone cannot make, for example between
        16-bit half float, 16-bit unsigned integer and 16-bit signed integer.
    * - ``ofx/creator``
      - :c:macro:`kOfxMetadataKeyCreator`
      - string X 1
      - Free text naming the person, organisation or application that created
        the source, carried through unchanged.
    * - ``ofx/project``
      - :c:macro:`kOfxMetadataKeyProject`
      - string X 1
      - Free text naming the production or project the source belongs to,
        carried through unchanged.
    * - ``ofx/copyright``
      - :c:macro:`kOfxMetadataKeyCopyright`
      - string X 1
      - The copyright notice attached to the source, carried through unchanged.
    * - ``ofx/comment``
      - :c:macro:`kOfxMetadataKeyComment`
      - string X 1
      - A free text comment attached to the source, carried through unchanged.
        May contain newlines, and is meant to be shown to a person rather than
        parsed.
    * - ``ofx/focallength``
      - :c:macro:`kOfxMetadataKeyFocalLength`
      - double X 1
      - Focal length of the taking lens in millimetres, actual rather than
        scaled to any reference format.
    * - ``ofx/fnumber``
      - :c:macro:`kOfxMetadataKeyFNumber`
      - double X 1
      - The f-number of the taking lens, the dimensionless ratio of focal length
        to entrance pupil diameter, so 2.8 denotes f/2.8. Not stops, not a
        T-stop.
    * - ``ofx/exposuretime``
      - :c:macro:`kOfxMetadataKeyExposureTime`
      - double X 1
      - Exposure time in seconds, so a 1/48 second exposure is
        0.020833333333333332. Not a reciprocal, not a fraction in a string and
        not a shutter angle.
    * - ``ofx/slateinfo``
      - :c:macro:`kOfxMetadataKeySlateInfo`
      - string X 1
      - Free text slate information, such as a scene and take identifier,
        carried through from the source unparsed.
    * - ``ofx/viewnames``
      - :c:macro:`kOfxMetadataKeyViewNames`
      - string X N
      - The names of the views present in the source, in the order the source
        stores them. **Not required**; see `The View Names Key`_.

The prefix ``ofx/`` itself is available as
:c:macro:`kOfxMetadataKeyPrefixStandard`.

Sequences and the File Path
---------------------------

A source is very often a numbered image sequence rather than a single movie
file, which raises the question of what ``ofx/filepath`` holds: the pattern, in
one of the several notations in use, or the concrete path of the frame actually
being read.

OFX specifies the **concrete resolved path**. When an image comes from a
numbered sequence, ``ofx/filepath`` is the path of the one file that holds that
image, with the frame number already substituted, for example::

    /shots/ab_010/plate/ab_010_plate.0087.exr

and not ``ab_010_plate.%04d.exr``, ``ab_010_plate.####.exr`` or any other
sequence notation. Two reasons decide it this way. Metadata is a property of a
single image, and the concrete path is the value that is actually a property of
that image, whereas the pattern is a property of the clip. And a plugin can open
a concrete path directly, whereas a pattern would force every plugin to
implement a parser for whichever notations hosts happen to emit.

This is what makes ``ofx/frame`` meaningful alongside it: the frame key holds
the number that was substituted into the pattern to produce the path, so the two
keys always agree, and a plugin that genuinely needs the pattern can reconstruct
it from the pair.

When an image comes from a container holding several images, such as a movie
file, ``ofx/filepath`` is the path of that container and every image read from
it carries the same value, while ``ofx/frame`` is the index of the image within
the container, counting from 0 for the first image stored in it.

When an image comes from a single-image file that is not part of a sequence,
``ofx/filepath`` is simply the path of that file and ``ofx/frame`` is omitted,
since there is no number that identifies the image within its source.

The View Names Key
------------------

``ofx/viewnames`` is reserved so that a host which knows the view names of a
source has a standard place to publish them, but it is **explicitly not
required**. A host need not populate it, whether or not it supports multiple
views, and a plugin must not depend on it being present.

In particular, a plugin must not treat the absence of ``ofx/viewnames`` as
meaning that the source is monoscopic, and must not use it to decide how many
views to process. A plugin that needs to know about views must use the
multi-view mechanisms of the API. The key is descriptive information for
display and bookkeeping, nothing more.

Vendor Keys
-----------

Anything not defined above is a vendor key and must be named in reverse DNS
form, using a domain that the party defining the key controls::

    com.example.mytool.regionOfInterest
    com.example.grading.lookName

The domain component makes collisions between independently developed hosts and
plugins impossible without co-operation, which a flat namespace of short names
would not. A plugin reading a vendor key it does not recognise should carry it
through untouched rather than dropping it, since metadata frequently passes
through effects that have no interest in it.

Reference
---------

.. doxygendefine:: kOfxMetadataKeyPrefixStandard

.. doxygendefine:: kOfxMetadataKeyPrefixExr

.. doxygendefine:: kOfxMetadataKeyPrefixExif

.. doxygendefine:: kOfxMetadataKeyPrefixDpx

.. doxygendefine:: kOfxMetadataKeyPrefixCin

.. doxygendefine:: kOfxMetadataKeyPrefixTiff

.. doxygendefine:: kOfxMetadataKeyPrefixQuickTime

.. doxygendefine:: kOfxMetadataKeyFilePath

.. doxygendefine:: kOfxMetadataKeySourceFrame

.. doxygendefine:: kOfxMetadataKeyTimecode

.. doxygendefine:: kOfxMetadataKeyEdgecode

.. doxygendefine:: kOfxMetadataKeyMTime

.. doxygendefine:: kOfxMetadataKeyFileSize

.. doxygendefine:: kOfxMetadataKeyPixelAspect

.. doxygendefine:: kOfxMetadataKeyFrameRate

.. doxygendefine:: kOfxMetadataKeyShutterAngle

.. doxygendefine:: kOfxMetadataKeyWidth

.. doxygendefine:: kOfxMetadataKeyHeight

.. doxygendefine:: kOfxMetadataKeyBitDepth

.. doxygendefine:: kOfxMetadataKeySampleType

.. doxygendefine:: kOfxMetadataKeyCreator

.. doxygendefine:: kOfxMetadataKeyProject

.. doxygendefine:: kOfxMetadataKeyCopyright

.. doxygendefine:: kOfxMetadataKeyComment

.. doxygendefine:: kOfxMetadataKeyFocalLength

.. doxygendefine:: kOfxMetadataKeyFNumber

.. doxygendefine:: kOfxMetadataKeyExposureTime

.. doxygendefine:: kOfxMetadataKeySlateInfo

.. doxygendefine:: kOfxMetadataKeyViewNames
