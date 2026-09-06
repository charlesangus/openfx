# metadataHost builds in both configurations: without OFX_SUPPORTS_METADATA only the
# fixture publication, the clip and image metadata reads and the composition checks
# are compiled out of it.
set(METADATA_PLUGIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/plugins")

if(OFX_SUPPORTS_METADATA)
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
endif()

add_executable(metadataHost
	metadataHost.cpp
	hostDemoClipInstance.cpp
	hostDemoEffectInstance.cpp
	hostDemoHostDescriptor.cpp
	hostDemoParamInstance.cpp)
target_link_libraries(metadataHost PRIVATE OfxHost)
target_compile_definitions(metadataHost
	PRIVATE METADATA_PLUGIN_DIR="${METADATA_PLUGIN_DIR}")
if(OFX_SUPPORTS_METADATA)
	add_dependencies(metadataHost metadataPlugin)
endif()
if(APPLE)
	# expat is single-arch here, so this executable can't be linked universal
	set_target_properties(metadataHost PROPERTIES
		OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

add_executable(hostDemo
	hostDemo.cpp
	hostDemoClipInstance.cpp
	hostDemoEffectInstance.cpp
	hostDemoHostDescriptor.cpp
	hostDemoParamInstance.cpp)
target_link_libraries(hostDemo PRIVATE OfxHost)
if(APPLE)
	# expat is single-arch here, so this executable can't be linked universal
	set_target_properties(hostDemo PROPERTIES
		OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()
