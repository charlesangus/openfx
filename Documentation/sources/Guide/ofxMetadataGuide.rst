.. SPDX-License-Identifier: CC-BY-4.0
.. _metadataGuide:

This guide covers the OFX metadata API from a plugin's side: finding out
whether a host can supply metadata, fetching the metadata attached to a
clip or an image, and reading the values. It uses the ``C++`` support
wrapper around :ref:`OfxMetadataSuiteV1`, ``OFX::MetadataSet``, declared in
`ofxsMetadata.h <https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/include/ofxsMetadata.h>`_
and handed out by the ``OFX::Clip`` and ``OFX::Image`` wrappers in
`ofxsImageEffect.h <https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/include/ofxsImageEffect.h>`_.
The model behind the API is described in :ref:`imageMetadata`. What a set
contains is up to the host: the suite defines no keys.

Metadata belongs to an image, not to a clip: the frame at time 5 of an
image sequence and the frame at time 6 can come from different files with
different tags and timecodes. Every read of a clip's metadata therefore
takes a time, an image's does not, since an image handle already denotes a
clip at one time.

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

    std::string path = metadata.getString("file_path");
    int frame         = metadata.getInt("source_frame", 0, -1);
    double frameRate  = metadata.getDouble("frame_rate", 0, 24.0);

The second argument is an index, since a key can carry more than one value,
for example a list of view names; ``getDimension`` reports
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

Choosing which key to read
==========================

The names in the examples above, ``file_path``, ``frame_rate`` and the rest,
are illustrations. Hosts differ in which keys they publish and what they call
them, so a plugin that consumes a value should not assume one name.

The simplest approach is a string parameter holding the key to read, with a
plausible default:

.. code:: c++

    std::string rateKey;
    rateKey_->getValue(rateKey);
    double rate = metadata.getDouble(rateKey, 0, 24.0);

The user sets it to whatever name their host uses. Either way, a missing key
reads back as the default, so a plugin should treat that as the normal case on
a host that does not publish it.

Worked examples
===============

Two plugins under ``Support/Plugins/`` exercise the read path end to end, each
passing its image through untouched.
`MetadataPrint
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataPrint/metadataPrint.cpp>`_
logs every key a clip carries, and
`MetadataView
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataView/metadataView.cpp>`_
filters that same metadata into a display parameter.
