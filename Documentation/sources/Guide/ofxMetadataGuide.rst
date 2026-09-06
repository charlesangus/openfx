.. SPDX-License-Identifier: CC-BY-4.0
.. _metadataGuide:

This guide covers the read side of the OFX metadata API: how a plugin
finds out whether a host can supply metadata at all, how it fetches the
metadata attached to a clip or an image, and how it enumerates and reads
the values once it has them. It uses the ``C++`` support wrapper,
:c:type:`OfxMetadataSuiteV1` and ``OFX::MetadataSet``, declared in
`ofxsMetadata.h <https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/include/ofxsMetadata.h>`_,
rather than the raw suite, since that is what almost every plugin should
use. Two complete, worked examples live in the repository and are
referred to throughout: `MetadataPrint
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataPrint/metadataPrint.cpp>`_,
which logs every key a clip carries, and `MetadataView
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataView/metadataView.cpp>`_,
which filters that same metadata into a parameter for display in a
host's UI.

Metadata belongs to an image, not a clip
=========================================

The metadata API is defined in
`ofxMetadata.h <https://github.com/AcademySoftwareFoundation/openfx/blob/main/include/ofxMetadata.h>`_,
which is also the place to look up the standard, host-independent key
vocabulary (:c:macro:`kOfxMetadataKeyFilePath`,
:c:macro:`kOfxMetadataKeyTimecode`, :c:macro:`kOfxMetadataKeyFrameRate`
and so on) and the rules around namespacing keys that are not part of
that vocabulary.

A clip does not have one fixed set of metadata: it has different
metadata at different times, since the frame at time 5 of an image
sequence and the frame at time 6 can come from different files with
different EXIF tags, timecodes and so on. This is why
``OfxMetadataSuiteV1::clipGetMetadata`` and ``OFX::Clip::getMetadata``
both take a time argument, while ``OfxMetadataSuiteV1::imageGetMetadata``
and ``OFX::Image::getMetadata`` do not — an image handle already denotes
a clip at one specific time, so there is nothing left to parameterise.

Checking whether the host can supply metadata
==============================================

Reading a key of a type the plugin didn't ask for relies on the generic
Property Suite to report what type the host actually holds it as, so a
host only qualifies as supporting metadata if it exposes both
:c:macro:`kOfxMetadataSuite` and ``OfxPropertySuiteV2``. The support
library checks both suites for you and folds them into a single flag:

.. code:: c++

    if(OFX::getImageEffectHostDescription()->supportsMetadata) {
      // ...
    }

.. note::

    As of this writing every shipping OFX host implements no metadata
    suite at all, so ``supportsMetadata`` is false everywhere a plugin
    will actually run. This is not a corner case to special-case away —
    it is the path every plugin using this API takes today, and the
    reason a host with no metadata suite gives back an *empty* set
    rather than an error: a plugin that skips the ``supportsMetadata``
    check and simply reads through an empty ``OFX::MetadataSet`` still
    behaves correctly, just as if the clip carried no metadata at all.
    Checking the flag first is only worth it to avoid the wasted round
    trip, not for correctness.

Fetching a ``MetadataSet``
===========================

``OFX::MetadataSet`` is an RAII wrapper around the metadata property set
handle: it releases the underlying handle when it goes out of scope, and
it is move-only, so a ``MetadataSet`` can be returned from a function or
held in a local but never copied. There are two ways to get one, matching
the two entry points of the raw suite:

.. code:: c++

    // the metadata a clip carries at a given time
    OFX::MetadataSet fromClip = OFX::MetadataSet::fetchFromClip(clipHandle, time);

    // the metadata already-fetched image carries
    OFX::MetadataSet fromImage = OFX::MetadataSet::fetchFromImage(imageHandle);

but a plugin working through the ``OFX::Clip`` and ``OFX::Image``
wrappers will normally never call these directly, and will instead use
the member functions that wrap them:

.. code:: c++

    OFX::MetadataSet clipMetadata = srcClip->getMetadata(time);
    OFX::MetadataSet imageMetadata = srcImage->getMetadata();

Both forms give back an empty set, rather than throwing, when the clip
or image simply has no metadata at that time, or when the host has no
metadata suite at all — ``MetadataSet::isValid()`` reports which case
you are in, though most plugins have no need to distinguish them, since
every getter on an empty set already gives back its default. The
fetches themselves are the exception to that rule: ``fetchFromClip`` and
``fetchFromImage`` throw ``OFX::Exception::Suite`` if the host's
underlying suite call itself fails, for example a bad handle. So does
every other structural call described below, ``entries()`` and
``keys()``; only the value getters are guaranteed never to throw.

Reading values
==============

Once you have a ``MetadataSet``, a value comes back as whatever type you
ask for, regardless of the type the host actually holds it as: a numeric
value read with ``getString`` comes back as text, and a string read with
``getDouble`` or ``getInt`` comes back as a number wherever the text
parses as one. A key that is absent, or whose value will not convert to
the type asked for, comes back as the ``defaultValue`` you passed rather
than as an error:

.. code:: c++

    std::string path = metadata.getString(kOfxMetadataKeyFilePath);
    int frame         = metadata.getInt(kOfxMetadataKeySourceFrame, 0, -1);
    double frameRate  = metadata.getDouble(kOfxMetadataKeyFrameRate, 0, 24.0);

The second argument to each of these is an index, since a key can carry
more than one value, for example :c:macro:`kOfxMetadataKeyViewNames`.
``getDimension`` tells you how many values a key has, and the ``N``
suffixed forms, ``getStringN``, ``getDoubleN`` and ``getIntN``, read
every value of a key back as a ``std::vector`` in one call rather than
one index at a time:

.. code:: c++

    std::vector<std::string> views = metadata.getStringN(kOfxMetadataKeyViewNames);

``has`` reports whether a key is present at all, and ``getType`` reports
the type the host actually holds it as — ``eMetadataTypeNone`` if the
key is absent or the host cannot report a type. None of these calls
ever throw: the worst that a lookup against an unknown key or an empty
set does is give back a default, an empty vector, or ``eMetadataTypeNone``.

Enumerating the keys of a set
===============================

The raw suite's ``metadataEnumerate`` visits every key of a metadata
property set through a callback, but its documentation is explicit that
no ordering is guaranteed, and that a host need not even enumerate the
same handle in the same order twice. ``MetadataSet::entries()`` and
``MetadataSet::keys()`` wrap that callback and sort the result, so a
plugin using them sees a stable, ascending order by key without doing
anything itself:

.. code:: c++

    for(const OFX::MetadataEntry &entry : metadata.entries()) {
      // entry.key, entry.type and entry.dimension, in ascending key order
    }

A plugin that calls ``OfxMetadataSuiteV1::metadataEnumerate`` directly,
rather than going through ``MetadataSet``, gets keys in whatever order
the host happens to produce them and must sort them itself if an order
is wanted.

Putting it together
====================

The `MetadataPrint
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataPrint/metadataPrint.cpp>`_
example plugin ties all of the above together: it checks
``supportsMetadata``, fetches the source clip's metadata at the current
render time, enumerates its entries in sorted order, and reads each
value back as the type the host reports for it, logging one line per
key:

.. code:: c++

    void
    MetadataPrintPlugin::logMetadata(double time)
    {
      if(!OFX::getImageEffectHostDescription()->supportsMetadata)
        return;

      const OFX::MetadataSet metadata = srcClip_->getMetadata(time);
      const std::vector<OFX::MetadataEntry> entries = metadata.entries();

      for(size_t i = 0; i < entries.size(); i++) {
        std::ostringstream line;

        line << "clip=" << srcClip_->name()
             << " frame=" << time
             << " key=" << entries[i].key
             << " type=" << typeName(entries[i].type)
             << " value=" << valueText(metadata, entries[i]);

        sendMessage(OFX::Message::eMessageLog, "", line.str());
      }
    }

The `MetadataView
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataView/metadataView.cpp>`_
example does the same fetch and enumeration, but instead of logging
every key it filters the entries against a string parameter and writes
the matching ones into a display parameter, so a host's UI can show a
user the metadata of whatever clip is connected — a good pattern to
follow for anything beyond a debug log.

.. _metadataNukeInterop:

Interop note: Nuke has no metadata suite today
===============================================

.. note::

    **This section describes a workaround for plugin authors targeting
    Nuke as it exists today. It is not part of the metadata API's
    design and should not be read back into the rest of this guide.**

As of Nuke 17.0.3, its OFX host implements no part of this API. Every
OFX symbol string was dumped out of ``libnuke-17.0.3.so`` and searched:
there is no :c:macro:`kOfxMetadataSuite`, no
:c:macro:`kOfxImageEffectActionGetMetadata`, and none of the other
metadata properties — not even DaVinci Resolve's vendor properties
``OfxImageEffectPropSrcFilePath`` or ``OfxImageEffectPropSrcFrame``,
which a plugin might otherwise fall back to. A plugin checking
``supportsMetadata`` on Nuke will simply, and correctly, see it as
false and get an empty ``MetadataSet`` from every clip.

The one way to get Nuke's metadata into an OFX plugin today is
indirect: Nuke maps an OFX string parameter onto its own
``EvalString_Knob``, which evaluates TCL expressions on read. A string
parameter whose value is a TCL ``[metadata ...]`` expression, for
example ``[metadata input/filename]``, is therefore handed to the
plugin already resolved to the concrete metadata value, not as the
literal expression text. This was verified end to end: an OFX plugin's
filename parameter was set to ``[metadata input/filename]``, an
upstream ``ModifyMetaData`` node was used to set a distinctive path for
that key, and the plugin received exactly that resolved path when it
read the parameter, with no knowledge on the plugin's part that TCL was
ever involved.

This is a real, working path to Nuke's metadata for a plugin author who
needs it now, but it depends entirely on Nuke's knob scripting and has
nothing to do with the OFX metadata suite described in the rest of this
guide. Treat it as a stopgap for one host, not as a model for how a
plugin should read metadata in general.
