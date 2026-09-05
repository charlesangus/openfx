# metadataHost.cpp #errors out if OFX_SUPPORTS_METADATA isn't defined, since
# it has nothing to exercise without it.
if(OFX_SUPPORTS_METADATA)
	add_executable(metadataHost
		metadataHost.cpp
		hostDemoClipInstance.cpp
		hostDemoEffectInstance.cpp
		hostDemoHostDescriptor.cpp
		hostDemoParamInstance.cpp)
	target_link_libraries(metadataHost PRIVATE OfxHost)
endif()

add_executable(hostDemo
	hostDemo.cpp
	hostDemoClipInstance.cpp
	hostDemoEffectInstance.cpp
	hostDemoHostDescriptor.cpp
	hostDemoParamInstance.cpp)
target_link_libraries(hostDemo PRIVATE OfxHost)
