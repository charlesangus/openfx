set(METADATA_PLUGIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/plugins")

# the plugins here are test fixtures, not shipped examples: each is laid out as a
# bundle in the build tree for metadataHost to load and is deliberately not
# installed, so add_ofx_plugin, which installs into PLUGIN_INSTALLDIR, is not
# used here
function(add_metadata_test_plugin TARGET)
	set(BINDIR "${METADATA_PLUGIN_DIR}/${TARGET}.ofx.bundle/Contents/${ARCHDIR}")
	if(APPLE)
		add_library(${TARGET} MODULE ${ARGN})
	else()
		add_library(${TARGET} SHARED ${ARGN})
	endif()
	target_compile_features(${TARGET} PRIVATE cxx_std_17)
	set_target_properties(${TARGET} PROPERTIES
		SUFFIX ".ofx"
		PREFIX ""
		C_VISIBILITY_PRESET hidden
		CXX_VISIBILITY_PRESET hidden
		LIBRARY_OUTPUT_DIRECTORY "${BINDIR}"
		RUNTIME_OUTPUT_DIRECTORY "${BINDIR}")
	# multi config generators otherwise append the config name to the output
	# directory, which would leave the bundle malformed
	foreach(config ${CMAKE_CONFIGURATION_TYPES})
		string(TOUPPER ${config} CONFIG)
		set_target_properties(${TARGET} PROPERTIES
			LIBRARY_OUTPUT_DIRECTORY_${CONFIG} "${BINDIR}"
			RUNTIME_OUTPUT_DIRECTORY_${CONFIG} "${BINDIR}")
	endforeach()
endfunction()

add_metadata_test_plugin(metadataPlugin metadataPlugin.cpp)
# no OfxHost here on purpose: the plugin talks the plugin facing C api only, so
# it cannot be caught out by a mismatch in the host library's build options
target_include_directories(metadataPlugin PRIVATE ${OFX_HEADER_DIR})

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

	add_test(NAME metadata.log-degraded.rejects-reading-plugin
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataPrint --check metadata-log-degraded
			--expect-failure)
	add_test(NAME metadata.display-degraded.rejects-reading-plugin
		COMMAND metadataHost --plugin-dir "${PLUGIN_INSTALLDIR}"
			--plugin-id net.sf.openfx.metadataView --check metadata-display-degraded
			--expect-failure)
endif()
