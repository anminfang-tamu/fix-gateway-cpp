# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.30

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /opt/homebrew/Cellar/cmake/3.30.0/bin/cmake

# The command to remove a file.
RM = /opt/homebrew/Cellar/cmake/3.30.0/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/anminfang/fix-gateway-cpp

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/anminfang/fix-gateway-cpp/build

# Include any dependencies generated for this target.
include CMakeFiles/message-integration-demo.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/message-integration-demo.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/message-integration-demo.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/message-integration-demo.dir/flags.make

CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.o: CMakeFiles/message-integration-demo.dir/flags.make
CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.o: /Users/anminfang/fix-gateway-cpp/demos/message_integration_demo.cpp
CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.o: CMakeFiles/message-integration-demo.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/Users/anminfang/fix-gateway-cpp/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.o"
	/Library/Developer/CommandLineTools/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.o -MF CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.o.d -o CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.o -c /Users/anminfang/fix-gateway-cpp/demos/message_integration_demo.cpp

CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.i"
	/Library/Developer/CommandLineTools/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/anminfang/fix-gateway-cpp/demos/message_integration_demo.cpp > CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.i

CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.s"
	/Library/Developer/CommandLineTools/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/anminfang/fix-gateway-cpp/demos/message_integration_demo.cpp -o CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.s

# Object files for target message-integration-demo
message__integration__demo_OBJECTS = \
"CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.o"

# External object files for target message-integration-demo
message__integration__demo_EXTERNAL_OBJECTS =

message-integration-demo: CMakeFiles/message-integration-demo.dir/demos/message_integration_demo.cpp.o
message-integration-demo: CMakeFiles/message-integration-demo.dir/build.make
message-integration-demo: src/protocol/libprotocol.a
message-integration-demo: src/utils/libutils.a
message-integration-demo: src/common/libcommon.a
message-integration-demo: CMakeFiles/message-integration-demo.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=/Users/anminfang/fix-gateway-cpp/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable message-integration-demo"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/message-integration-demo.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/message-integration-demo.dir/build: message-integration-demo
.PHONY : CMakeFiles/message-integration-demo.dir/build

CMakeFiles/message-integration-demo.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/message-integration-demo.dir/cmake_clean.cmake
.PHONY : CMakeFiles/message-integration-demo.dir/clean

CMakeFiles/message-integration-demo.dir/depend:
	cd /Users/anminfang/fix-gateway-cpp/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/anminfang/fix-gateway-cpp /Users/anminfang/fix-gateway-cpp /Users/anminfang/fix-gateway-cpp/build /Users/anminfang/fix-gateway-cpp/build /Users/anminfang/fix-gateway-cpp/build/CMakeFiles/message-integration-demo.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/message-integration-demo.dir/depend

