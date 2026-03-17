cmake_minimum_required(VERSION 3.13)

project(codecrafters-shell)

file(GLOB_RECURSE SOURCE_FILES src/*.c src/*.h)

set(CMAKE_C_STANDARD 23) # Enable the C23 standard

add_executable(shell ${SOURCE_FILES})

find_library(READLINE_LIB readline)
if(READLINE_LIB)
	target_link_libraries(shell PRIVATE ${READLINE_LIB})
endif()
