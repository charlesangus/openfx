.. SPDX-License-Identifier: CC-BY-4.0
.. _metadataGuide:

This guide covers the OFX metadata API from a plugin's side: finding out
whether a host can supply metadata, fetching the metadata attached to a
clip or an image, reading the values, and contributing metadata to the
effect's output. It uses the ``C++`` support wrappers around
:ref:`OfxMetadataSuiteV1`, ``OFX::MetadataSet``, ``OFX::MetadataSetter``
and ``OFX::MetadataInheritanceSetter``, declared in
`ofxsMetadata.h <https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/include/ofxsMetadata.h>`_ and
`ofxsImageEffect.h <https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/include/ofxsImageEffect.h>`_.
The model behind the API, the standard key vocabulary and the namespacing
rules for every other key are described in :ref:`imageMetadata`.

Metadata belongs to an image, not to a clip: the frame at time 5 of an
image sequence and the frame at time 6 can come from different files with
different tags and timecodes. Every read of a clip's metadata therefore
takes a time, an image's does not, since an image handle already denotes a
clip at one time, and the action an effect contributes through is always
time-parameterised.

Clip and image metadata
=======================

Checking whether the host can supply metadata
---------------------------------------------

A host supports metadata exactly when it exposes :c:macro:`kOfxMetadataSuite`,
which the support library folds into one flag:

.. code:: c++

    if(OFX::getImageEffectHostDescription()->supportsMetadata) {
      // ...
    }

Checking it is an optimisation, not a requirement: a host with no metadata
suite gives back an empty ``OFX::MetadataSet`` from every clip and image,
and every getter on an empty set returns its default.

Fetching a ``MetadataSet``
--------------------------

``OFX::MetadataSet`` is a move-only RAII wrapper around a metadata property
set handle, released when it goes out of scope. The ``OFX::Clip`` and
``OFX::Image`` wrappers each hand one out:

.. code:: c++

    OFX::MetadataSet clipMetadata = srcClip->getMetadata(time);
    OFX::MetadataSet imageMetadata = srcImage->getMetadata();

Both give back an empty set, rather than throwing, when the clip or image
has no metadata or the host has no metadata suite; ``isValid()`` tells the
two apart. The fetches, ``entries()`` and ``keys()`` throw
``OFX::Exception::Suite`` if the host fails the underlying suite call; the
value getters never throw.

Reading values
--------------

A value comes back as whatever type you ask for, regardless of the type the
host holds it as: a number read with ``getString`` comes back as text, and a
string read with ``getDouble`` or ``getInt`` comes back as a number wherever
the text parses as one. A key that is absent, or whose value will not
convert, comes back as the default you passed rather than as an error:

.. code:: c++

    std::string path = metadata.getString(kOfxMetadataKeyFilePath);
    int frame         = metadata.getInt(kOfxMetadataKeySourceFrame, 0, -1);
    double frameRate  = metadata.getDouble(kOfxMetadataKeyFrameRate, 0, 24.0);

The second argument is an index, since a key can carry more than one value,
for example :c:macro:`kOfxMetadataKeyViewNames`; ``getDimension`` reports
how many, and ``getStringN``, ``getDoubleN`` and ``getIntN`` read all of
them into a ``std::vector`` in one call. ``has`` reports whether a key is
present, and ``getType`` its type, ``eMetadataTypeNone`` if it is absent.

The suite's ``metadataEnumerate`` visits every key through a callback that
carries the key's name, type and dimension, in no guaranteed order.
``MetadataSet::entries()`` and ``MetadataSet::keys()`` wrap that callback
and sort the result, so a plugin sees a stable, ascending order by key.

Putting it together
-------------------

``MetadataPrint`` ties the read path together: it fetches the source clip's
metadata at the render time, enumerates its entries, and logs one line per
key, reading each value back as the type the host reports for it:

.. literalinclude:: ../../../Support/Plugins/MetadataPrint/metadataPrint.cpp
   :language: c++
   :start-after: // guide: begin logMetadata
   :end-before: // guide: end logMetadata

``MetadataView`` does the same fetch and enumeration but filters the
entries against a string parameter and writes the matching ones into a
disabled display parameter, so a host's UI can show them. Because a render
must not write a parameter, it composes the display from ``changedParam``
and ``changedClip`` instead.

Contributing metadata
=====================

A plugin that wants to add keys to its output, or to control what its
output inherits from its inputs, overrides ``getMetadata``:

.. code:: c++

    virtual void getMetadata(const OFX::MetadataArguments &args,
                             OFX::MetadataSetter &metadata,
                             OFX::MetadataInheritanceSetter &inheritance);

``args`` carries only ``time``. The two setters wrap two different property
sets: the keys the effect contributes go through ``metadata`` into the
host-owned set that arrives in the action's ``inArgs`` under
:c:macro:`kOfxImageEffectPropMetadataSet`; which input clips the output
inherits from, and which of their keys survive, go through ``inheritance``
into the action's ``outArgs``. The output's metadata is the inherited keys
with the contributed keys written over them, so a key the effect writes
replaces the inherited value of the same key.

Contributing keys with ``MetadataSetter``
-----------------------------------------

``metadata`` arrives empty and is the only metadata set an effect may write
to. It offers ``setString``, ``setDouble`` and ``setInt``, the ``N`` forms
``setStringN``, ``setDoubleN`` and ``setIntN`` for writing every value of a
key at once, and ``copyFrom``, which re-emits every entry of a
``MetadataSet`` under its original key. There is no indexed setter to match
the indexed getters: a key that does not yet exist has no dimension to
index into, and the generic Property Suite cannot create one, so every
setter replaces a key's value and dimension as a whole. A setter that fails,
because the host has no metadata suite or rejects the call, does nothing
and never throws.

Pass-through of an input's metadata is not done by copying keys into
``metadata``; it is the host's default, steered through ``inheritance`` as
described below. ``metadata`` is for a value the effect computes.
``MetadataTimeCode`` counts a timecode on from a start code, reading the
frame rate off its source's metadata when asked to, and writes a different
:c:macro:`kOfxMetadataKeyTimecode` at every frame alongside the
:c:macro:`kOfxMetadataKeyFrameRate` it counted at:

.. literalinclude:: ../../../Support/Plugins/MetadataTimeCode/metadataTimeCode.cpp
   :language: c++
   :start-after: // guide: begin getMetadata
   :end-before: // guide: end getMetadata

Choosing what the output inherits with ``MetadataInheritanceSetter``
--------------------------------------------------------------------

``setSourceClips`` nominates which input clips the output's metadata is
composed from, and in what order: the list is read in increasing
precedence, so the last clip named wins wherever two carry the same key,
and an empty list inherits nothing from any clip. ``setRetainedKeys``
selects, for one clip, which of its keys survive; a key left off the list
is dropped exactly as if the clip never carried it, and this is the only
way to remove an inherited key, since ``metadata`` holds only what the
effect contributes. ``getSourceClips`` and ``getRetainedKeys`` read the
current lists back.

Before the effect touches them the lists hold the host's defaults, which
follow the first *connected* input clip in the order the effect described
them: the source list names that clip alone, its retained-keys list holds
every key it carries, and every other clip's list is empty. An effect that
calls neither setter therefore inherits all of the first connected clip's
metadata and nothing from any other input, and an unconnected clip
contributes nothing even when the source list names it.

Dropping a key means reading the default list back and setting it again
without that key, as ``MetadataModify`` does for every key it removes:

.. literalinclude:: ../../../Support/Plugins/MetadataModify/metadataModify.cpp
   :language: c++
   :dedent: 2
   :start-after: // guide: begin dropRemovedKeys
   :end-before: // guide: end dropRemovedKeys

The retained-keys property has no C identifier: its name is composed at
describe time, one property per input clip the effect describes, by
appending the clip's name to ``OfxImageClipPropMetadataRetainedKeys_``.
``MetadataInheritanceSetter`` hides that composition, and ``setRetainedKeys``
and ``getRetainedKeys`` throw ``OFX::Exception::PropertyUnknownToHost`` for
a clip the effect never defined.

A multi-input effect has to say which of its clips the output inherits from
only when it wants a composition other than the host's default of the first
connected clip alone. ``MetadataCopy`` has a ``Source`` and a ``Mask``, orders
them by a mode parameter, leaves an unconnected ``Mask`` out of the list, and
sets each named clip's retained keys from that clip's own metadata, since only
the first connected clip's list is pre-filled by the host:

.. literalinclude:: ../../../Support/Plugins/MetadataCopy/metadataCopy.cpp
   :language: c++
   :start-after: // guide: begin getMetadata
   :end-before: // guide: end getMetadata

What the host does with the answer
----------------------------------

The support library answers the action with :c:macro:`kOfxStatOK` when a
setter on ``metadata`` succeeded or a setter on ``inheritance`` was called,
and with :c:macro:`kOfxStatReplyDefault` otherwise; ``didSomething()`` on
either reports its half. On :c:macro:`kOfxStatReplyDefault` the host reads
back neither set and composes the output's metadata from its defaults, so
an effect that calls neither setter, or does not override ``getMetadata``
at all, leaves the host to its default inheritance.

There is no property through which a plugin invalidates a previous answer.
An answer for a given time is valid only while the input metadata it was
composed from, the effect's parameter values and the effect's clip
connections remain unchanged, and the host must re-issue
:c:macro:`kOfxImageEffectActionGetMetadata` after any of those change.

The handles behind ``metadata`` and ``inheritance`` are owned by the host
for the duration of the action only: neither setter releases anything, and
neither argument may be kept or referred to after ``getMetadata`` returns.

Worked examples
===============

Six plugins under ``Support/Plugins/`` exercise the API end to end, each
passing its image through untouched.
`MetadataPrint
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataPrint/metadataPrint.cpp>`_
and `MetadataView
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataView/metadataView.cpp>`_
cover the read path: logging every key a clip carries, and filtering that
same metadata into a display parameter.
`MetadataTimeCode
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataTimeCode/metadataTimeCode.cpp>`_
is the write path: a timecode and a frame rate contributed fresh on every
call, so the value changes from one frame to the next.
`MetadataModify
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataModify/metadataModify.cpp>`_
applies an ordered, user-authored list of ``set`` and ``remove``
operations to its source clip's inherited metadata, working the whole list
out before either setter is touched.
`MetadataCopy
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataCopy/metadataCopy.cpp>`_
and `MetadataCompare
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataCompare/metadataCompare.cpp>`_
have two input clips, ``Source`` and ``Mask``, and declare only
``eContextGeneral``, since a filter context would let a host instantiate
them with one clip. MetadataCopy composes the two inputs' metadata under
four selectable modes, each clip's contribution filtered through its own
pattern parameter; MetadataCompare writes nothing, reporting into a display
parameter which keys are on one side only and which disagree.
