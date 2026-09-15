set(METADATA_PLUGIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/plugins")

# metadataPlugin is a test fixture, not a shipped example: it is laid out as a
# bundle in the build tree for metadataHost to load and is deliberately not
# installed, so add_ofx_plugin, which installs into PLUGIN_INSTALLDIR, is not
# used here.
set(METADATA_PLUGIN_BINDIR
	"${METADATA_PLUGIN_DIR}/metadataPlugin.ofx.bundle/Contents/${ARCHDIR}")

if(APPLE)
	add_library(metadataPlugin MODULE metadataPlugin.cpp)
else()
	add_library(metadataPlugin SHARED metadataPlugin.cpp)
endif()
# no OfxHost here on purpose: the plugin talks the plugin facing C api only, so
# it cannot be caught out by a mismatch in the host library's build options
target_include_directories(metadataPlugin PRIVATE ${OFX_HEADER_DIR})
target_compile_features(metadataPlugin PRIVATE cxx_std_17)
set_target_properties(metadataPlugin PROPERTIES
	SUFFIX ".ofx"
	PREFIX ""
	C_VISIBILITY_PRESET hidden
	CXX_VISIBILITY_PRESET hidden
	LIBRARY_OUTPUT_DIRECTORY "${METADATA_PLUGIN_BINDIR}"
	RUNTIME_OUTPUT_DIRECTORY "${METADATA_PLUGIN_BINDIR}")
# multi config generators otherwise append the config name to the output
# directory, which would leave the bundle malformed
foreach(config ${CMAKE_CONFIGURATION_TYPES})
	string(TOUPPER ${config} CONFIG)
	set_target_properties(metadataPlugin PROPERTIES
		LIBRARY_OUTPUT_DIRECTORY_${CONFIG} "${METADATA_PLUGIN_BINDIR}"
		RUNTIME_OUTPUT_DIRECTORY_${CONFIG} "${METADATA_PLUGIN_BINDIR}")
endforeach()

add_executable(metadataHost
	metadataHost.cpp
	../examples/hostDemoClipInstance.cpp
	../examples/hostDemoEffectInstance.cpp
	../examples/hostDemoHostDescriptor.cpp
	../examples/hostDemoParamInstance.cpp)
target_include_directories(metadataHost PRIVATE ../examples)
target_link_libraries(metadataHost PRIVATE OfxHost)
target_compile_definitions(metadataHost
	PRIVATE METADATA_PLUGIN_DIR="${METADATA_PLUGIN_DIR}")
add_dependencies(metadataHost metadataPlugin)
if(APPLE)
	# expat is single-arch here, so this executable can't be linked universal
	set_target_properties(metadataHost PROPERTIES
		OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

add_test(NAME metadata.harness COMMAND metadataHost)
add_test(NAME metadata.fixture
	COMMAND metadataHost --plugin-id net.sf.openfx.metadataPlugin)
add_test(NAME metadata.log.rejects-silent-plugin
	COMMAND metadataHost --plugin-id net.sf.openfx.metadataPlugin --check metadata-log
		--expect-failure)

# the rest load example plugins from PLUGIN_INSTALLDIR, which only this tree's own
# install fills; registered without BUILD_EXAMPLE_PLUGINS they would find nothing
# there, or whatever an earlier install of some other tree left behind
if(BUILD_EXAMPLE_PLUGINS)
	add_test(NAME metadata.log
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataPrint --check metadata-log)
	add_test(NAME metadata.display
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataView --check metadata-display)
	add_test(NAME metadata.contribute
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataContribute --check metadata-contribute)
	add_test(NAME metadata.modify
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataModify --check metadata-modify)
	add_test(NAME metadata.timecode
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataTimeCode --check metadata-timecode)
	add_test(NAME metadata.copy
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataCopy --check metadata-copy)
	add_test(NAME metadata.compare
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataCompare
			--upstream net.sf.openfx.metadataModify --check metadata-compare)

	add_test(NAME metadata.log-degraded.rejects-reading-plugin
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataPrint --check metadata-log-degraded
			--expect-failure)
	add_test(NAME metadata.display-degraded.rejects-reading-plugin
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataView --check metadata-display-degraded
			--expect-failure)
	add_test(NAME metadata.contribute.rejects-non-contributing-plugin
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataPrint --check metadata-contribute
			--expect-failure)
	add_test(NAME metadata.modify.rejects-non-editing-plugin
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataPrint --check metadata-modify
			--expect-failure)
	add_test(NAME metadata.timecode.rejects-no-timecode
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataContribute --check metadata-timecode
			--expect-failure)
	add_test(NAME metadata.copy.rejects-single-input
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataContribute --check metadata-copy
			--expect-failure)
	add_test(NAME metadata.compare.rejects-no-upstream
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataCompare --check metadata-compare
			--expect-failure)
	add_test(NAME metadata.compare-degraded.rejects-comparing-plugin
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataCompare
			--upstream net.sf.openfx.metadataModify --check metadata-compare-degraded
			--expect-failure)

	add_test(NAME metadata.chain
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataView
			--upstream net.sf.openfx.metadataContribute --check metadata-chain)
	add_test(NAME metadata.chain.rejects-no-upstream
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataView --check metadata-chain
			--expect-failure)
	add_test(NAME metadata.chain.rejects-empty-head
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataView
			--upstream net.sf.openfx.metadataPrint --check metadata-chain
			--expect-failure)

	add_test(NAME metadata.graph
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataView
			--upstream net.sf.openfx.metadataModify
			--upstream net.sf.openfx.metadataTimeCode
			--upstream net.sf.openfx.metadataCopy --check metadata-graph)
	add_test(NAME metadata.graph.rejects-short-chain
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataView
			--upstream net.sf.openfx.metadataModify
			--upstream net.sf.openfx.metadataTimeCode --check metadata-graph
			--expect-failure)
endif()
