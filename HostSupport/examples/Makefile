DEBUG ?= false
EXPAT_INCLUDE ?= expat-2.0.1/lib
OBJSUF ?= .o
LIBSUF ?= .a
LIBPREFIX ?= lib
LIBNAME ?= ofxHost
EXPAT_LIB_PATH ?= .
OS = $(shell uname)
DEBUG ?= false
OFXSLIB = $(LIBPREFIX)$(LIBNAME)$(LIBSUF)

ifeq ($(DEBUG), true)
  DST_DIR = $(OS)-debug
  OPTIMISE ?= -g -Wall
  EXPATFLAG = --enable-debug
else ifeq ($(DEBUG), instrument)
  DST_DIR = $(OS)-instrument
  OPTIMISE ?= -g -O3 -Wall
else
  DST_DIR = $(OS)-release
  OPTIMISE ?= -O2 -Wall
  EXPATFLAGS = --disable-debug
endif

# the library and everything built here must agree on this, as it changes the layout
# of the clip and effect classes metadataHost derives from
METADATA_FLAGS ?= -DOFX_SUPPORTS_METADATA

INCFLAGS = -I../include -I../../include -I../$(EXPAT_INCLUDE) 
CXXFLAGS = $(INCFLAGS) $(METADATA_FLAGS) $(OPTIMISE)

HOST_DEMO_FILES = $(DST_DIR)/hostDemo.o \
	$(DST_DIR)/hostDemoClipInstance.o     \
	$(DST_DIR)/hostDemoEffectInstance.o   \
	$(DST_DIR)/hostDemoHostDescriptor.o   \
	$(DST_DIR)/hostDemoParamInstance.o    

METADATA_HOST_FILES = $(DST_DIR)/metadataHost.o \
	$(DST_DIR)/hostDemoClipInstance.o     \
	$(DST_DIR)/hostDemoEffectInstance.o   \
	$(DST_DIR)/hostDemoHostDescriptor.o   \
	$(DST_DIR)/hostDemoParamInstance.o    

OBJECT_FILES = $(HOST_DEMO_FILES) $(DST_DIR)/metadataHost.o

all : $(DST_DIR)/hostDemo $(DST_DIR)/cacheDemo $(DST_DIR)/metadataHost

clean :
	rm -f $(DST_DIR)/*.o $(DST_DIR)/cacheDemo $(DST_DIR)/hostDemo $(DST_DIR)/metadataHost
	cd ..; make clean DEBUG=$(DEBUG) EXPAT_INCLUDE=$(EXPAT_INCLUDE) OBJSUF=$(OBJSUF) LIBSUF=$(LIBSUF) \
	LIBPREFIX=$(LIBPREFIX) LIBNAME=$(LIBNAME); 

$(OFXSLIB):
	cd ..; make DEBUG=$(DEBUG) EXPAT_INCLUDE=$(EXPAT_INCLUDE) OBJSUF=$(OBJSUF) LIBSUF=$(LIBSUF) \
	LIBPREFIX=$(LIBPREFIX) LIBNAME=$(LIBNAME) OPTIMISE="$(OPTIMISE) $(METADATA_FLAGS)"; 


$(OBJECT_FILES) : $(DST_DIR)/%.o : %.cpp
	mkdir -p $(DST_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(DST_DIR)/cacheDemo : cacheDemo.cpp $(OFXSLIB)
	mkdir -p $(DST_DIR)
	$(CXX) $(CXXFLAGS) cacheDemo.cpp -o $(DST_DIR)/cacheDemo -L../$(DST_DIR) -lofxHost -L$(EXPAT_LIB_PATH) -lexpat -ldl

$(DST_DIR)/hostDemo : $(HOST_DEMO_FILES)  $(OFXSLIB)
	mkdir -p $(DST_DIR)
	$(CXX) $(CXXFLAGS) $(HOST_DEMO_FILES) -o $(DST_DIR)/hostDemo -L../$(DST_DIR) -lofxHost -L$(EXPAT_LIB_PATH) -lexpat -ldl

$(DST_DIR)/metadataHost : $(METADATA_HOST_FILES)  $(OFXSLIB)
	mkdir -p $(DST_DIR)
	$(CXX) $(CXXFLAGS) $(METADATA_HOST_FILES) -o $(DST_DIR)/metadataHost -L../$(DST_DIR) -lofxHost -L$(EXPAT_LIB_PATH) -lexpat -ldl
