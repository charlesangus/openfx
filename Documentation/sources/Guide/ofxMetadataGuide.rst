.. SPDX-License-Identifier: CC-BY-4.0
.. _metadataGuide:

This guide covers the OFX metadata API: how a plugin finds out whether
a host can supply metadata at all, how it fetches metadata attached to a
clip or an image, reads the values once it has them, and contributes
its own metadata to its output. It uses the ``C++`` support wrappers,
:c:type:`OfxMetadataSuiteV1`, ``OFX::MetadataSet``,
``OFX::MetadataSetBuilder`` and ``OFX::MetadataInheritanceSetter``,
declared in
`ofxsMetadata.h <https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/include/ofxsMetadata.h>`_ and
`ofxsImageEffect.h <https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/include/ofxsImageEffect.h>`_,
rather than the raw suite, since that is what almost every plugin should
use. Three complete, worked examples live in the repository and are
referred to throughout: `MetadataPrint
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataPrint/metadataPrint.cpp>`_,
which logs every key a clip carries, and `MetadataView
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataView/metadataView.cpp>`_,
which filters that same metadata into a parameter for display in a
host's UI, and `MetadataContribute
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataContribute/metadataContribute.cpp>`_,
which contributes keys of every supported type to its output and can
drop a key from what it inherits.

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

Contributing metadata
=======================

Everything so far has been about reading metadata a host already has. A
plugin that wants to add its own keys, or control what its output
inherits from its inputs, overrides ``getMetadata``:

.. code:: c++

    virtual bool getMetadata(const OFX::MetadataArguments &args,
                              OFX::MetadataSetBuilder &metadata,
                              OFX::MetadataInheritanceSetter &inheritance);

The default implementation returns false and traps nothing, so a plugin
that never overrides it is unaffected: the host falls through to its own
inheritance rules exactly as if the metadata action did not exist.
``OFX::MetadataArguments`` carries only ``time``, since, as established
above, metadata is a property of an image and this action is always
time-parameterised.

Unlike every read call covered so far, contributing metadata means
writing to two separate property sets, and the support classes for them
are not interchangeable:

- the keys a plugin contributes go into a metadata property set owned by
  the host, arriving in the action's ``inArgs`` under
  :c:macro:`kOfxImageEffectPropMetadataSet`, and a plugin writes to it
  through the ``metadata`` argument, an ``OFX::MetadataSetBuilder``;
- which source clips the output inherits from, and which of each
  clip's keys are retained, are set in the action's ``outArgs``, through
  the ``inheritance`` argument, an ``OFX::MetadataInheritanceSetter``.

A plugin using the support library never fetches either property set
itself: the two arguments already wrap them by the time ``getMetadata``
is called.

Contributing keys with ``MetadataSetBuilder``
-----------------------------------------------

``metadata`` arrives empty, and is the only metadata property set an
effect may write to. It offers ``setString``, ``setDouble``, ``setInt``
and the ``N``-suffixed forms ``setStringN``, ``setDoubleN`` and
``setIntN`` for writing every value of a key at once, plus ``copyFrom``
for re-emitting every entry of a ``MetadataSet`` into the contribution
set under its own original key; it takes no destination key, so it is
not a way to rename a key while copying it. Ordinary pass-through of
an input's metadata is not done by copying keys into ``metadata`` at
all — it is the host's default behaviour, steered through
``MetadataInheritanceSetter`` and covered below in "Choosing what the
output inherits with ``MetadataInheritanceSetter``". There is no
indexed setter to match ``MetadataSet``'s indexed getters: a key that
does not already exist has no dimension to index into, and a key
cannot be created through the generic Property Suite, which fails on
a property it has never heard of. Every setter therefore replaces a
key's value and dimension as a whole rather than writing part of it,
whether the key is being created or already exists.

.. code:: c++

    metadata.setString(kOfxMetadataKeyCreator, "My Plugin");
    metadata.setDoubleN("com.example.myplugin.weights",
                         std::vector<double>({1.0, 0.5, 0.25}));

``didSomething()`` reports whether any call on the builder has yet
succeeded, which matters because returning false from ``getMetadata``
discards everything written to it — see below.

Choosing what the output inherits with ``MetadataInheritanceSetter``
------------------------------------------------------------------------

Contributed keys and inherited keys are not the same thing, and they do
not merge inside ``metadata``: an effect's output metadata is the
inherited metadata composed first, then everything the effect wrote
through ``metadata`` layered on top, so a key an effect contributes
always wins over the same key inherited from a source clip. The
inherited half of that composition is entirely what ``inheritance``
describes.

``setSourceClips`` nominates which of the effect's input clips the
output's metadata is composed from, and in what order: the list is read
in increasing precedence, so the last clip named wins wherever two
clips in the list carry the same key. A single-input effect normally has
nothing to do here — the host already defaults the list to that one
clip — but a multi-input effect, for example one with a ``Source`` and
a ``Mask``, has to call ``setSourceClips`` to say which of them, and in
which order, the output should inherit from. Passing an empty list is
how an effect declares that its output inherits no metadata at all, from
any clip.

Within a clip that is in the source list, ``setRetainedKeys`` selects
which of that clip's keys survive into the inherited metadata; a key
left off the list is dropped from that clip exactly as if the clip
never carried it. This is also the only way to delete an inherited key
— there is no removal call on ``metadata``, because ``metadata`` holds
only what the effect itself contributes, not what it inherits. Deleting
something the effect does not want passed through is entirely the
retained-keys list's job:

.. code:: c++

    const std::vector<std::string> retained = inheritance.getRetainedKeys(*srcClip_);
    std::vector<std::string> kept;

    for(size_t i = 0; i < retained.size(); i++) {
      if(retained[i] != dropKey)
        kept.push_back(retained[i]);
    }

    inheritance.setRetainedKeys(*srcClip_, kept);

Calling ``getRetainedKeys`` first, as above, and then calling
``setRetainedKeys`` with that same list minus the one key to drop, is
the pattern to reach for: it starts from the host's default rather than
from an assumption about what that default is. The retained-keys
property itself has no C identifier to name it with, because its
property name is composed at the time the effect is described, one
property per attached input clip, by appending that clip's name to
``OfxImageClipPropMetadataRetainedKeys_`` — precisely the composition
``MetadataInheritanceSetter`` exists to hide, so a plugin calls
``setRetainedKeys(clip, keys)`` and never needs to know the resulting
property's name at all.

``setRetainedKeys`` throws ``OFX::Exception::PropertyUnknownToHost`` if
asked for a clip the effect never defined.

Returning true, and why there is no invalidation property
--------------------------------------------------------------

``getMetadata`` has to return true for anything written to either
``metadata`` or ``inheritance`` to take effect. Returning false is not
a no-op: it tells the host the action was not trapped, so the host
discards both property sets, whatever they contain, and falls through
to composing the output's metadata under its own default rules. A
plugin that contributes keys or narrows what it inherits and then
forgets to return true loses all of it silently, with no error to
signal that anything was ignored.

There is no property to say when a previous answer from this action has
gone stale. The host does not need one: it re-calls
:c:macro:`kOfxImageEffectActionGetMetadata` whenever the effect's
parameter or input state changes, using the same hash it already
maintains for its render cache, so there is nothing for a plugin to
invalidate by hand.

Finally, the handle backing both ``metadata`` and ``inheritance`` is
owned by the host for the duration of the action only. As with a
``MetadataSet``, it must never be released by the plugin, but unlike a
``MetadataSet`` there is no RAII wrapper doing that for you because
there is nothing to release: the host makes the handle behind
``metadata`` reject an explicit release outright, and the handle behind
``inheritance`` is simply dead the moment the action returns, so neither
argument should be kept, copied out of, or referred to again after
``getMetadata`` has returned.

The `MetadataContribute
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataContribute/metadataContribute.cpp>`_
example plugin ties this together: it contributes a handful of its own
keys unconditionally, then, depending on a mode parameter, either leaves
its source clip's inheritance untouched, calls ``getRetainedKeys`` and
``setRetainedKeys`` to drop one named key from it, or calls
``setSourceClips`` with an empty list to inherit nothing at all — and
returns true in every one of those branches, since even the modes that
inherit nothing still contributed keys through ``metadata``.

Worked examples
=================

Seven complete plugins under ``Support/Plugins/`` exercise this API end to
end. `MetadataPrint
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataPrint/metadataPrint.cpp>`_
and `MetadataView
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataView/metadataView.cpp>`_,
walked through above in "Putting it together", cover the plain read path:
logging every key a clip carries, and filtering that same metadata into a
read-only display parameter. `MetadataContribute
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataContribute/metadataContribute.cpp>`_,
walked through above at the end of "Contributing metadata", is the
minimal write path: a fixed contribution through every
``MetadataSetBuilder`` entry point, paired with each of the three ways
to steer inheritance — untouched, one key dropped, or nothing inherited
at all.

`MetadataModify
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataModify/metadataModify.cpp>`_
takes that further: an ordered, user-authored list of ``set`` and
``remove`` operations is resolved against a single source clip's
inherited metadata, a later operation on a key overriding an earlier
one, before either ``metadata`` or ``inheritance`` is touched.
Every removal it performs, like every removal in MetadataContribute,
is expressed by leaving a key off the list passed to
``setRetainedKeys`` — there is no suite call that deletes an inherited
key directly, since ``inheritance`` has no such call and ``metadata``
holds only what the effect contributes, never what it inherits.

`MetadataTimeCode
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataTimeCode/metadataTimeCode.cpp>`_
computes a timecode from a start code and a frame rate and contributes
it fresh on every call to ``getMetadata``, so the value it writes
changes from one frame to the next: the concrete demonstration that
metadata belongs to an image, not to a clip as a whole.

`MetadataCopy
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataCopy/metadataCopy.cpp>`_
and `MetadataCompare
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/Support/Plugins/MetadataCompare/metadataCompare.cpp>`_
are the two examples with more than one input clip, ``Source`` and
``Mask``; both declare only ``eContextGeneral``, since declaring
``eContextFilter`` as well would let a host instantiate them with a
single clip. MetadataCopy combines the two inputs' metadata under four
selectable modes — source only, mask only, or either one layered over
the other — filtering each clip's contribution through its own pattern
parameter before retaining it. MetadataCompare takes the same two
inputs but writes nothing: it reads both clips' metadata independently
and reports, into a read-only parameter, which keys belong to only one
side and which are present on both but disagree.

MetadataCopy is also where a fact about the host's default becomes
unavoidable rather than academic: as covered in
`HostSupport/src/ofxhImageEffect.cpp
<https://github.com/AcademySoftwareFoundation/openfx/blob/main/HostSupport/src/ofxhImageEffect.cpp>`_,
the host pre-populates a retained-keys default, the whole of that
clip's key set, only for the first input clip an effect describes;
every other clip's list starts empty. A single-input effect never
notices, but MetadataCopy has two inputs, so it cannot rely on
``getRetainedKeys`` to hand back candidates for ``Mask`` — filtering an
empty list would contribute nothing from it at all — and instead reads
``Mask``'s own metadata directly and calls ``setRetainedKeys`` on it
from scratch, exactly as it does for ``Source``.

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
