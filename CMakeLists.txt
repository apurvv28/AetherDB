cmake_minimum_required(VERSION 3.14)
project(AetherDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add definitions for Windows
if(WIN32)
  add_definitions(-D_WIN32_WINNT=0x0601) # Target Windows 7 or later
endif()

# Common library containing shared utilities like Socket wrappers
add_library(aether_common STATIC
  common/src/socket.cpp
)

target_include_directories(aether_common PUBLIC
  common/include
)

if(WIN32)
  target_link_libraries(aether_common PUBLIC ws2_32)
endif()

# Storage Service daemon executable
add_executable(aether-storage-service
  services/storage/src/disk_manager.cpp
  services/storage/src/storage_service.cpp
  services/storage/main.cpp
)

target_include_directories(aether-storage-service PRIVATE
  services/storage/include
)

target_link_libraries(aether-storage-service PRIVATE aether_common)

# Testing configurations
enable_testing()

add_executable(disk_manager_test
  tests/storage/disk_manager_test.cpp
  services/storage/src/disk_manager.cpp
)

target_include_directories(disk_manager_test PRIVATE
  services/storage/include
  common/include
  tests
)

if(WIN32)
  target_link_libraries(disk_manager_test PRIVATE ws2_32)
endif()

add_test(NAME disk_manager_test COMMAND disk_manager_test)
