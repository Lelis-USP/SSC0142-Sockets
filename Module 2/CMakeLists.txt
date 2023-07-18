cmake_minimum_required(VERSION 3.25)
project(TP2_Redes)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(-fsanitize=address -g)
add_link_options(-fsanitize=address -g)


# Add config files
file(GLOB_RECURSE COMMON_FILES "src/common/*.cpp" "src/common/*.c" "src/common/*.h" "src/common/*.hpp")
add_library(common ${COMMON_FILES})
target_include_directories(common PUBLIC src/common)

# Add client files
file(GLOB_RECURSE CLIENT_FILES "src/client/*.cpp" "src/client/*.c" "src/client/*.h" "src/client/*.hpp")
add_executable(client ${CLIENT_FILES})
target_link_libraries(client common)
target_include_directories(client PUBLIC src/client)

# Add network files
file(GLOB_RECURSE SERVER_FILES "src/server/*.cpp" "src/server/*.c" "src/server/*.h" "src/server/*.hpp")
add_executable(server ${SERVER_FILES})
target_link_libraries(server common)
target_include_directories(server PUBLIC src/server)
