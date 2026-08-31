cmake_minimum_required(VERSION 3.10)
project(gbm-hybris VERSION 1.0 LANGUAGES C)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(LIBRARY_NAME hybris_gbm)
set(SOURCE_FILES main.c)
include_directories(/usr/include/android/ /usr/include/libdrm)
link_directories(/usr/lib64)
add_library(${LIBRARY_NAME} SHARED ${SOURCE_FILES})
set_target_properties(${LIBRARY_NAME} PROPERTIES PREFIX "")
target_link_libraries(${LIBRARY_NAME} PRIVATE gralloc)
#set_target_properties(${LIBRARY_NAME} PROPERTIES
#    VERSION ${PROJECT_VERSION}
#    SOVERSION 1
#    OUTPUT_NAME "${LIBRARY_NAME}"
#)
install(TARGETS ${LIBRARY_NAME}
    LIBRARY DESTINATION /usr/lib/aarch64-linux-gnu/gbm/
)
